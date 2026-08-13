#pragma once

#include <cstdint>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerReverbBlend
 * @ingroup apps
 * @brief Single-knob reverb combo map (SHIFT+POT_5): dry→wet crossfade folded
 *        with decay length. Pure logic, host-tested (see test_reverb_blend).
 * @author Summoner (CHORD-GEN) build session
 * @date 2026-08-13
 *
 * The reverb has no separate wet/dry control in the design; this folds "how much
 * reverb" and "how long the tail" onto one knob with a clear inflection at 50%:
 *
 * - 0%            : fully dry (wet = 0) — the reverb is bypassed in the mix, so a
 *                   clean dry chord is always reachable (fixes "no dry signal").
 * - 0 → 50%       : decay held short; wet crossfades 0 → full (dry → very wet).
 * - 50%           : fully wet, shortest decay — a short, drenched reverb.
 * - 50 → 100%     : wet held full; decay grows short → long (very wet, long tail).
 *
 * Continuous at 50% (wet reaches full exactly as decay begins to grow), so the
 * transition between the two halves is smooth. Same "fold at 50%" idiom as the
 * planned Phase-10 folded controls (SummonerGroove / SummonerLfoShape style).
 */
struct SummonerReverbBlend
{
    /** @brief Reverb parameters for one knob position. */
    struct Result
    {
        float decay; ///< ShimmerReverb decay (feedback), clamped to [kShortDecay, kLongDecay]
        q15_t wet;   ///< Dry↔wet crossfade: 0 = fully dry, Q15_MAX = fully wet
    };

    static constexpr float kShortDecay = 0.60f; ///< Tightest tail (bottom half + 50%)
    static constexpr float kLongDecay = 0.97f;  ///< Longest tail (100%); == ShimmerReverb::kMaxDecay

    /**
     * @brief Map a knob value (0..Q15_MAX) to reverb decay + wet mix.
     * @param x Knob position as q15 (use pot_to_q15 on the FancyPot value).
     * @return Decay and wet-mix for the current position.
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
        if (x <= Q15_HALF)
        {
            // Bottom half: short decay, wet ramps 0 → full across [0, Q15_HALF].
            r.decay = kShortDecay;
            int32_t wet = static_cast<int32_t>(x) * Q15_MAX / Q15_HALF;
            r.wet = static_cast<q15_t>(wet > Q15_MAX ? Q15_MAX : wet);
        }
        else
        {
            // Top half: full wet, decay grows short → long across (Q15_HALF, Q15_MAX].
            r.wet = Q15_MAX;
            const float t = static_cast<float>(x - Q15_HALF) /
                            static_cast<float>(Q15_MAX - Q15_HALF);
            r.decay = kShortDecay + (kLongDecay - kShortDecay) * t;
        }
        return r;
    }
};

} // namespace kastle2
