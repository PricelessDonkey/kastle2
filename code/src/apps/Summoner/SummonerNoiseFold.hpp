#pragma once

#include <array>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerNoiseFold
 * @ingroup apps
 * @brief Folded noise-blend knob map (SHIFT+BANK+POT_6): blend amount folded
 *        with a noise-specific attack. Pure logic, host-tested
 *        (see test_summoner_noise_fold). Resolves open decision 2 (constant vs
 *        attack-weighted noise) by putting both on one knob.
 * @author Summoner (CHORD-GEN) Phase 10
 * @date 2026-08-13
 *
 * Folds "how much noise" and "how the noise enters" onto one knob, inflecting at
 * 50% (CHORD-GEN.md Noise blend §):
 *
 * - Amount: 0 (no noise) at 0% → ramps to max at 50% → held max across 50→100%.
 * - Noise attack, lower vs upper half:
 *   - 0–50%: no attack — noise is blended at full level from the note onset
 *     (immediate, percussive chiff; this is the old constant-blend behavior,
 *     rescoped to the bottom half).
 *   - 50→100%: a noise-specific attack envelope ramps in, its time scaling with
 *     knob position (none at 50%, a little at 60%, a lot at 100%), so the noise
 *     swells in later and later after the transient.
 *
 * "No noise at 100%" is perceptual: amount is still max, but the attack is long
 * enough that little noise enters during a typical note — a slow airy bloom only
 * under sustained notes, effectively clean at onset. Continuous at 50% (upper-
 * half attack starts at 0). Same "fold at 50%" idiom as SummonerEnvelope.
 *
 * The attack envelope itself lives in the app (per-voice ramp); this maps the
 * knob to the equal-power blend amount and the attack time in seconds.
 */
struct SummonerNoiseFold
{
    /** @brief Noise parameters for one knob position. */
    struct Result
    {
        q15_t amount; ///< Equal-power blend amount 0..Q15_MAX (max across the upper half)
        float attack; ///< Noise attack time in seconds (0 across the lower half)
    };

    static constexpr float kMaxAttack = 1.5f; ///< Slowest noise swell (at 100%)

    /**
     * @brief Map a knob value (0..Q15_MAX) to blend amount + noise attack time.
     * @param x Knob position as q15 (use pot_to_q15 on the combo slot value).
     * @return Blend amount and attack time for the current position.
     */
    static Result Compute(q15_t x)
    {
        if (x < 0)
        {
            x = 0;
        }
        if (x > Q15_MAX)
        {
            x = Q15_MAX;
        }

        Result r;

        // Amount: ramps 0 → full across the lower half, then held full.
        if (x <= Q15_HALF)
        {
            int32_t amt = static_cast<int32_t>(x) * Q15_MAX / Q15_HALF;
            r.amount = static_cast<q15_t>(amt > Q15_MAX ? Q15_MAX : amt);
        }
        else
        {
            r.amount = Q15_MAX;
        }

        // Attack: instant (0) below/at center; ramps in across the upper half
        // with the time scaling by position.
        if (x <= Q15_HALF)
        {
            r.attack = 0.0f;
        }
        else
        {
            const float p = static_cast<float>(x - Q15_HALF) /
                            static_cast<float>(Q15_MAX - Q15_HALF);
            r.attack = InterpAnchors(kAttackAnchors, p);
        }
        return r;
    }

private:
    /// Attack anchors over upper-half position 0..1 (seconds).
    static constexpr std::array<float, 5> kAttackAnchors = {0.0f, 0.05f, 0.2f, 0.6f, 1.5f};

    /** @brief Piecewise-linear interpolation over 5 equally-spaced anchors, t in [0,1]. */
    static constexpr float InterpAnchors(const std::array<float, 5> &a, float t)
    {
        if (t <= 0.0f)
        {
            return a[0];
        }
        if (t >= 1.0f)
        {
            return a[4];
        }
        const float scaled = t * 4.0f;
        const int32_t seg = static_cast<int32_t>(scaled);
        const float frac = scaled - static_cast<float>(seg);
        return a[seg] + (a[seg + 1] - a[seg]) * frac;
    }
};

} // namespace kastle2
