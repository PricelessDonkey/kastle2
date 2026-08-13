#pragma once

#include <cstddef>
#include <cstdint>
#include "common/dsp/math/math_utils.hpp"
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"

namespace kastle2
{

/**
 * @class SummonerLfoShape
 * @ingroup apps
 * @brief Reshapes Base's LFO triangle at the rate knob's (POT_7) outer zones.
 * @author sam
 * @date 2026-08-13
 *
 * The middle 20–80% of POT_7 is stock rate behaviour (pass the triangle
 * through untouched). Past those boundaries the rate is meant to freeze — see
 * EffectiveRatePot(), which the app feeds back into Base's rate maps — and the
 * knob travel instead crossfades in an alternate wave shape:
 *
 * - **0–20% (wander):** once per LFO cycle draw a fresh random target and slew
 *   toward it over the cycle instead of tracing a clean triangle. Amount ramps
 *   0 (at 20%) → full (at 0%). Organic, phase-locked drift.
 * - **80–100% (sample & hold):** once per LFO cycle draw a random value and
 *   hold it flat until the next wrap. Amount ramps 0 (at 80%) → full (at 100%).
 *   Choppy stepped modulation.
 *
 * Both zones redraw only on a phase wrap (one random draw per LFO cycle, not
 * per audio buffer). Reshaping happens *before* the BANK+POT_7
 * amplitude/polarity scaling, so that layer's contract is untouched. See
 * CHORD-GEN.md "LFO Shape Extremes". Pure logic — the app owns the phase-wrap
 * signal and the WhiteNoise source; this class holds only the reshape state.
 */
class SummonerLfoShape
{
public:
    /// Base's GetLfoTriangle() range is 0..1023 (10-bit).
    static constexpr int32_t kTriMax = 1023;

    /// POT_7 zone boundaries (0..4095). Below/above these the shape morphs in.
    static constexpr int32_t kLowZoneEnd = 819;     ///< pot(0.2)
    static constexpr int32_t kHighZoneStart = 3276; ///< pot(0.8)

    /**
     * @brief Wander crossfade amount for a given rate-knob position.
     * @param pot POT_7 value, 0..4095.
     * @return 1.0 at pot 0, ramping to 0.0 at the 20% boundary, 0 above it.
     */
    static float WanderAmount(const int32_t pot)
    {
        if (pot >= kLowZoneEnd)
        {
            return 0.0f;
        }
        return static_cast<float>(kLowZoneEnd - pot) / static_cast<float>(kLowZoneEnd);
    }

    /**
     * @brief Sample-&-hold crossfade amount for a given rate-knob position.
     * @param pot POT_7 value, 0..4095.
     * @return 0 below the 80% boundary, ramping to 1.0 at pot 4095.
     */
    static float SampleHoldAmount(const int32_t pot)
    {
        if (pot <= kHighZoneStart)
        {
            return 0.0f;
        }
        return static_cast<float>(pot - kHighZoneStart) /
               static_cast<float>(kPotMax - kHighZoneStart);
    }

    /**
     * @brief The rate-knob value the LFO rate should be pinned to.
     *
     * Clamps POT_7 into [20%, 80%]: past either boundary the rate stops
     * changing (frozen at the boundary's rate) and the shape morph takes over.
     * The app feeds this into Base's own rate maps only while in an outer zone.
     * @param pot POT_7 value, 0..4095.
     * @return pot clamped to [kLowZoneEnd, kHighZoneStart].
     */
    static int32_t EffectiveRatePot(const int32_t pot)
    {
        if (pot < kLowZoneEnd)
        {
            return kLowZoneEnd;
        }
        if (pot > kHighZoneStart)
        {
            return kHighZoneStart;
        }
        return pot;
    }

    /** @brief True while POT_7 sits in either reshape zone. */
    static bool InExtremeZone(const int32_t pot)
    {
        return pot < kLowZoneEnd || pot > kHighZoneStart;
    }

    /**
     * @brief Reshapes one buffer's triangle value.
     * @param raw_tri Base's triangle output, 0..kTriMax.
     * @param cycle_wrapped True on the buffer where the LFO phase wrapped —
     *        triggers the once-per-cycle random redraw and period measurement.
     * @param pot POT_7 value, 0..4095 (selects zone / crossfade amount).
     * @param noise Seeded random source (drawn only on a wrap).
     * @return Reshaped triangle, 0..kTriMax. Bit-identical to @p raw_tri in the
     *         stock 20–80% zone.
     */
    int32_t Process(const int32_t raw_tri, const bool cycle_wrapped,
                    const int32_t pot, WhiteNoise &noise)
    {
        // Track the cycle length in buffers so wander reaches its target over
        // one period regardless of rate; redraw both shapes on each wrap.
        buffers_since_wrap_++;
        if (cycle_wrapped)
        {
            if (buffers_since_wrap_ > 0)
            {
                period_ = buffers_since_wrap_;
            }
            buffers_since_wrap_ = 0;
            wander_target_ = RandTri(noise);
            sh_value_ = RandTri(noise);
        }

        // Slew toward the wander target across the measured period.
        wander_current_ += (wander_target_ - wander_current_) / static_cast<float>(period_);

        const float wander_amt = WanderAmount(pot);
        const float sh_amt = SampleHoldAmount(pot);

        // Stock zone: pass through untouched (no float rounding drift).
        if (wander_amt <= 0.0f && sh_amt <= 0.0f)
        {
            return raw_tri;
        }

        const float raw = static_cast<float>(raw_tri);
        float out = raw;
        if (wander_amt > 0.0f)
        {
            out = raw + (wander_current_ - raw) * wander_amt;
        }
        else
        {
            out = raw + (static_cast<float>(sh_value_) - raw) * sh_amt;
        }

        int32_t result = static_cast<int32_t>(out + 0.5f);
        return constrain(result, 0, kTriMax);
    }

    /** @brief Resets the reshape state (fresh flat output). */
    void Reset()
    {
        buffers_since_wrap_ = 0;
        period_ = kDefaultPeriod;
        wander_current_ = 0.0f;
        wander_target_ = 0.0f;
        sh_value_ = 0;
    }

private:
    static constexpr int32_t kPotMax = 4095;
    static constexpr int32_t kDefaultPeriod = 64; ///< buffers, until first wrap measured

    /// Maps a WhiteNoise sample (q15, -32768..32767) to 0..kTriMax.
    static int32_t RandTri(WhiteNoise &noise)
    {
        const int32_t u = static_cast<int32_t>(noise.Process()) + 32768; // 0..65535
        return (u * kTriMax) / 65535;
    }

    int32_t buffers_since_wrap_ = 0;
    int32_t period_ = kDefaultPeriod;
    float wander_current_ = 0.0f;
    float wander_target_ = 0.0f;
    int32_t sh_value_ = 0;
};
}
