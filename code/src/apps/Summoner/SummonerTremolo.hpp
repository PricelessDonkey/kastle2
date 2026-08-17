#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerTremolo
 * @ingroup apps
 * @brief Tempo-synced tremolo gate (SHIFT+POT_1), replacing portamento. Pure
 *        logic, host-tested (see test_summoner_tremolo).
 * @author Summoner (CHORD-GEN) Phase 11
 * @date 2026-08-16
 *
 * A square gate chopping the chord at a ratio of the **clock step period**, so
 * dotted and triplet settings cut across whatever the Euclidean sequencer is
 * playing instead of merely doubling its pulse. Rhythmic device, not a wobble
 * (CHORD-GEN.md Tremolo §).
 *
 * The knob folds at 50%, the same idiom as SummonerEnvelope / SummonerNoiseFold:
 *
 * - **Lower half:** ratio steps from OFF (index 0, at the very bottom) up
 *   through the table; the gate applies to all four voices.
 * - **Upper half:** the same ratios again from index 1 upward, but voice 0 (the
 *   root/bass) is exempt — the low end sustains while the upper voices chop.
 *   Index 0 is deliberately absent up here: a bass-exempt "off" would be
 *   indistinguishable from off.
 *
 * Voice 0 is already the privileged voice everywhere else in this firmware
 * (detune leaves it true, the broken-chord skip never drops it, CV_OUT tracks
 * it), so the exemption is consistent rather than a special case.
 *
 * Periods are integer {num, den} multiples of the step period, so
 * `period_frames = step_frames * num / den` — recomputed on knob/tempo change in
 * UiLoop, never in the audio loop. `OnClockTick()` re-locks the phase every step
 * so the gate cannot drift against the sequencer. A zero/unknown step period
 * holds the gate fully open, so nothing goes silent before the first clock tick.
 */
class SummonerTremolo
{
public:
    /// Gate edge slew — hard enough to read as a gate, slewed enough that a Q15
    /// jump from floor to full never clicks.
    static constexpr float kSlewMs = 2.5f;

    /// Number of entries in kRatios, including the OFF entry at index 0.
    static constexpr size_t kNumRatios = 10;

    /** @brief One ratio: the gate period as num/den of the clock step period. */
    struct Ratio
    {
        int32_t num; ///< Numerator (0 for the OFF entry)
        int32_t den; ///< Denominator
    };

    /// Gate periods as multiples of the clock step period (CHORD-GEN.md table):
    /// OFF, 1/2, 1/4, 1/4 dotted, 1/4 triplet, 1/8, 1/8 dotted, 1/8 triplet,
    /// 1/16, 1/16 triplet.
    static constexpr std::array<Ratio, kNumRatios> kRatios = {{
        {0, 1}, // 0: OFF
        {2, 1}, // 1: 1/2          — 2 steps
        {1, 1}, // 2: 1/4          — 1 step
        {3, 2}, // 3: 1/4 dotted   — 3/2 step
        {2, 3}, // 4: 1/4 triplet  — 2/3 step
        {1, 2}, // 5: 1/8          — 1/2 step
        {3, 4}, // 6: 1/8 dotted   — 3/4 step
        {1, 3}, // 7: 1/8 triplet  — 1/3 step
        {1, 4}, // 8: 1/16         — 1/4 step
        {1, 6}, // 9: 1/16 triplet — 1/6 step
    }};

    /** @brief What one knob position means: which ratio, and whether the bass is exempt. */
    struct Setting
    {
        size_t ratio_index; ///< Index into kRatios; 0 = tremolo off
        bool bass_exempt;   ///< True in the upper half: voice 0 is not gated
    };

    /**
     * @brief Maps a knob value to a ratio + bass-exempt flag, folding at 50%.
     * @param x Knob position as q15 (use pot_to_q15 on the pot value).
     * @return The quantized setting for that position.
     */
    static Setting FromQ15(q15_t x)
    {
        if (x < 0)
        {
            x = 0;
        }
        if (x > Q15_MAX)
        {
            x = Q15_MAX;
        }

        Setting s;
        if (x <= Q15_HALF)
        {
            // Lower half: the full table, OFF included, gating every voice.
            const int32_t span = static_cast<int32_t>(Q15_HALF) + 1;
            int32_t idx = static_cast<int32_t>(x) * static_cast<int32_t>(kNumRatios) / span;
            if (idx > static_cast<int32_t>(kNumRatios) - 1)
            {
                idx = static_cast<int32_t>(kNumRatios) - 1;
            }
            s.ratio_index = static_cast<size_t>(idx);
            s.bass_exempt = false;
        }
        else
        {
            // Upper half: ratios 1..N-1 (no OFF), bass sustains through the chop.
            const int32_t up = static_cast<int32_t>(x) - static_cast<int32_t>(Q15_HALF);
            const int32_t span = static_cast<int32_t>(Q15_MAX) - static_cast<int32_t>(Q15_HALF);
            const int32_t count = static_cast<int32_t>(kNumRatios) - 1;
            int32_t idx = 1 + up * count / (span + 1);
            if (idx > count)
            {
                idx = count;
            }
            s.ratio_index = static_cast<size_t>(idx);
            s.bass_exempt = true;
        }
        return s;
    }

    /**
     * @brief Initializes the gate for a sample rate.
     * @param sample_rate Audio sample rate in Hz (sets the edge slew increment).
     */
    void Init(const int32_t sample_rate)
    {
        const float slew_samples = kSlewMs * 0.001f * static_cast<float>(sample_rate);
        slew_inc_ = (slew_samples < 1.0f)
                        ? Q15_MAX
                        : static_cast<int32_t>(static_cast<float>(Q15_MAX) / slew_samples);
        if (slew_inc_ < 1)
        {
            slew_inc_ = 1;
        }
        period_frames_ = 0;
        phase_ = 0;
        depth_ = 0;
        gain_ = Q15_MAX;
    }

    /**
     * @brief Sets the gate period from the measured clock step and a ratio.
     * @param step_frames Frames between clock ticks (0 = not measured yet).
     * @param ratio_index Index into kRatios; 0 (OFF) holds the gate open.
     */
    void SetPeriodFrames(const int32_t step_frames, const size_t ratio_index)
    {
        if (step_frames <= 0 || ratio_index == 0 || ratio_index >= kNumRatios)
        {
            period_frames_ = 0;
            phase_ = 0;
            return;
        }
        const Ratio r = kRatios[ratio_index];
        int32_t p = step_frames * r.num / r.den;
        if (p < 2)
        {
            p = 2; // a sub-2-frame gate is meaningless; keep the phase math sane
        }
        if (p != period_frames_)
        {
            period_frames_ = p;
            if (phase_ >= period_frames_)
            {
                phase_ = 0;
            }
        }
    }

    /** @brief Re-locks the gate phase to the clock so it cannot drift. */
    void OnClockTick()
    {
        phase_ = 0;
    }

    /**
     * @brief Sets how far the gate's "off" phase drops.
     * @param depth 0 = no effect (gate stays at Q15_MAX), Q15_MAX = full silence.
     */
    void SetDepth(const q15_t depth)
    {
        depth_ = depth < 0 ? 0 : depth;
    }

    /**
     * @brief Advances one sample.
     * @return The slewed gate gain to multiply a voice by, 0..Q15_MAX.
     */
    q15_t Tick()
    {
        int32_t target = Q15_MAX;
        if (period_frames_ > 0)
        {
            // Square: first half of the period on, second half at the floor.
            if (phase_ >= (period_frames_ >> 1))
            {
                target = Q15_MAX - depth_;
            }
            phase_++;
            if (phase_ >= period_frames_)
            {
                phase_ = 0;
            }
        }

        // Slew toward the target so no edge is a hard Q15 step.
        if (gain_ < target)
        {
            gain_ += slew_inc_;
            if (gain_ > target)
            {
                gain_ = target;
            }
        }
        else if (gain_ > target)
        {
            gain_ -= slew_inc_;
            if (gain_ < target)
            {
                gain_ = target;
            }
        }
        return static_cast<q15_t>(gain_);
    }

    /** @brief Per-sample slew increment (the anti-click bound). */
    int32_t GetSlewInc() const
    {
        return slew_inc_;
    }

    /** @brief Current gate period in frames; 0 = gate held open. */
    int32_t GetPeriodFrames() const
    {
        return period_frames_;
    }

private:
    int32_t slew_inc_ = Q15_MAX;
    int32_t period_frames_ = 0;
    int32_t phase_ = 0;
    int32_t depth_ = 0;
    int32_t gain_ = Q15_MAX;
};

} // namespace kastle2
