#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerEnvelope
 * @ingroup apps
 * @brief Folded envelope knob map (POT_4 / LENGTH): a V-shaped decay length
 *        coupled with an upper-half-only attack. Pure logic, host-tested
 *        (see test_summoner_envelope).
 * @author Summoner (CHORD-GEN) Phase 10
 * @date 2026-08-13
 *
 * Folds "how long the note rings" and "how slowly it blooms" onto POT_4, with a
 * clear inflection at the 50% center (CHORD-GEN.md Envelope §):
 *
 * - Decay/release, V-shaped: long at 0% → shortest at 50% → long at 100%. Both
 *   halves lengthen away from center; 50% is the tightest pluck. The anchor
 *   curve is the pre-fold kMapDecay shape (0.03 → 4.0s) run over the distance
 *   from center, so the knob feel matches what POT_4 used to give end-to-end.
 * - Attack, upper half only: instant (kMinAttack) across 0–50%; then the attack
 *   time ramps in across 50→100%, scaling with knob position — none at 50%, a
 *   little at 60%, a lot at 100%. Continuous at 50% (upper-half attack starts at
 *   kMinAttack).
 *
 * Resulting feel: 0% = long decay + instant attack (struck pad that rings out);
 * 50% = short decay + instant attack (tight stab); 100% = long decay + long
 * attack (slow reverse-style swell that blooms in and rings out).
 *
 * The CV (PARAM_3, LENGTH MOD, attenuated by SHIFT+POT_4) sums into the knob
 * position *before* the fold, so an LFO on LENGTH MOD traverses the same V +
 * attack curve. Same "fold at 50%" idiom as SummonerReverbBlend / SummonerGroove.
 */
struct SummonerEnvelope
{
    /** @brief Envelope times for one knob position. */
    struct Result
    {
        float decay;  ///< Decay/release time in seconds (V-shaped, [kMinDecay, kMaxDecay])
        float attack; ///< Attack time in seconds ([kMinAttack, kMaxAttack]; kMinAttack across the lower half)
    };

    static constexpr float kMinDecay = 0.03f;  ///< Tightest pluck (at 50%)
    static constexpr float kMaxDecay = 4.0f;   ///< Longest tail (at 0% and 100%)
    static constexpr float kMinAttack = 0.002f; ///< Struck feel (lower half + 50%)
    static constexpr float kMaxAttack = 1.0f;  ///< Slow pad swell (at 100%)

    /**
     * @brief Map a knob value (0..Q15_MAX) to decay + attack times.
     * @param x Knob position as q15 (use pot_to_q15 on the summed FancyPot + CV).
     * @return Decay and attack times for the current position.
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

        // Decay: fold at 50%, interpolate the kMapDecay-shaped anchor curve over
        // the normalised distance from center (0 at 50%, 1 at either end).
        const float dist = std::abs(static_cast<float>(x) - static_cast<float>(Q15_HALF)) /
                           static_cast<float>(Q15_HALF);
        r.decay = InterpAnchors(kDecayAnchors, dist > 1.0f ? 1.0f : dist);

        // Attack: instant below/at center; ramps in across the upper half with
        // the time scaling by position.
        if (x <= Q15_HALF)
        {
            r.attack = kMinAttack;
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
    /// Decay anchors over distance-from-center 0..1 (== the pre-fold kMapDecay
    /// curve: 0.03 / 0.12 / 0.4 / 1.2 / 4.0s at 0 / 0.25 / 0.5 / 0.75 / 1.0).
    static constexpr std::array<float, 5> kDecayAnchors = {0.03f, 0.12f, 0.4f, 1.2f, 4.0f};

    /// Attack anchors over upper-half position 0..1 (== the retired kMapAttack
    /// curve: 0.002 / 0.02 / 0.1 / 0.4 / 1.0s).
    static constexpr std::array<float, 5> kAttackAnchors = {0.002f, 0.02f, 0.1f, 0.4f, 1.0f};

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
        const float scaled = t * 4.0f; // 4 segments between 5 anchors
        const int32_t seg = static_cast<int32_t>(scaled);
        const float frac = scaled - static_cast<float>(seg);
        return a[seg] + (a[seg + 1] - a[seg]) * frac;
    }
};

} // namespace kastle2
