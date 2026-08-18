#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerStrumSync
 * @ingroup apps
 * @brief Tempo-synced strum spacing (SHIFT+POT_3). Pure logic, host-tested
 *        (see test_summoner_strum_sync).
 * @author Summoner (CHORD-GEN) Phase 13
 * @date 2026-08-18
 *
 * Turns the strum-speed knob into a stepped table of **ratios of the clock step
 * period**, so the cascade between adjacent voices keeps its rhythmic meaning at
 * any tempo. Before 2026-08-18 the knob was an absolute 0->300ms curve
 * (`kMapStrum`), which smeared across two steps at slow tempos and collapsed to
 * a block chord at fast ones.
 *
 * Same idiom as @ref SummonerTremolo — integer `{num, den}` multiples of the
 * step period, computed in UiLoop at fire time, never in the audio loop — but
 * this knob does **not** fold at 50%: it is one ascending table across the whole
 * travel, from a block chord at the hard stop to a half-step cascade at the top.
 * Dotted and triplet entries sit between the straight subdivisions so a strum
 * can push against the sequencer rather than merely subdividing it.
 *
 * The spacing is between *adjacent* voices, so with four voices the whole chord
 * spans `3 x spacing`: at the top entry (1/2 step) that is 1.5 steps, i.e. the
 * slow-arpeggio end where the chord is still unrolling when the next one fires.
 */
class SummonerStrumSync
{
public:
    /// Number of entries in kRatios, including the block-chord entry at index 0.
    static constexpr size_t kNumRatios = 10;

    /**
     * @brief Step period assumed before the clock has been measured.
     *
     * @ref SummonerGroove::GetPeriodFrames returns 0 until two clock ticks have
     * been seen (a fraction of a second after power-on). Rather than firing the
     * first chord or two as an unrequested block chord, fall back to a nominal
     * ~4 Hz step at 44kHz so early fires already strum sensibly.
     */
    static constexpr int32_t kFallbackStepFrames = 11025;

    /** @brief One ratio: voice-to-voice spacing as num/den of the clock step period. */
    struct Ratio
    {
        int32_t num; ///< Numerator (0 for the block-chord entry)
        int32_t den; ///< Denominator
    };

    /// Spacings as fractions of the clock step period, ascending:
    /// block chord, 1/32, 1/16 triplet, 1/16, 1/8 triplet, 1/8, 1/4 triplet,
    /// 1/8 dotted, 1/4, 1/2 of a step. Strictly ascending, which is why the
    /// 1/4 triplet (1/6) sits *below* the 1/8 dotted (3/16).
    static constexpr std::array<Ratio, kNumRatios> kRatios = {{
        {0, 1},  // 0: block chord    — all voices together
        {1, 32}, // 1: 1/32 step
        {1, 24}, // 2: 1/16 triplet   — (1/16) x 2/3
        {1, 16}, // 3: 1/16 step
        {1, 12}, // 4: 1/8 triplet    — (1/8) x 2/3
        {1, 8},  // 5: 1/8 step
        {1, 6},  // 6: 1/4 triplet    — (1/4) x 2/3
        {3, 16}, // 7: 1/8 dotted     — (1/8) x 3/2, longer than a 1/4 triplet
        {1, 4},  // 8: 1/4 step
        {1, 2},  // 9: 1/2 step       — 4 voices span 1.5 steps
    }};

    /**
     * @brief Maps a knob value to an index into @ref kRatios.
     * @param x Knob position as q15 (use pot_to_q15 on the pot value).
     * @return Index 0..kNumRatios-1; 0 = block chord.
     *
     * Ten equal zones across the whole travel — no 50% fold. Out-of-range input
     * (the pot summed with a CV elsewhere in the app) clamps to the ends.
     */
    static size_t IndexFromQ15(q15_t x)
    {
        if (x < 0)
        {
            x = 0;
        }
        if (x > Q15_MAX)
        {
            x = Q15_MAX;
        }

        const int32_t span = static_cast<int32_t>(Q15_MAX) + 1;
        int32_t idx = static_cast<int32_t>(x) * static_cast<int32_t>(kNumRatios) / span;
        if (idx > static_cast<int32_t>(kNumRatios) - 1)
        {
            idx = static_cast<int32_t>(kNumRatios) - 1;
        }
        return static_cast<size_t>(idx);
    }

    /**
     * @brief Voice-to-voice spacing in audio frames for a knob position.
     * @param x Knob position as q15.
     * @param step_frames Measured clock step period (SummonerGroove::GetPeriodFrames);
     *        0 or negative falls back to @ref kFallbackStepFrames.
     * @return Frames between adjacent voices; 0 for a block chord.
     *
     * @ref SummonerStrum::Fire clamps its own maximum, so an absurdly slow clock
     * cannot schedule an unbounded cascade.
     */
    static int32_t FramesFromQ15(const q15_t x, int32_t step_frames)
    {
        if (step_frames <= 0)
        {
            step_frames = kFallbackStepFrames;
        }
        const Ratio &r = kRatios[IndexFromQ15(x)];
        if (r.num == 0)
        {
            return 0;
        }
        return static_cast<int32_t>(static_cast<int64_t>(step_frames) * r.num / r.den);
    }
};

} // namespace kastle2
