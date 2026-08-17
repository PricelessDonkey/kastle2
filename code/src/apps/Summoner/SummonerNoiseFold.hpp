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
 * - Amount is a **tent** peaking at 50% (revised 2026-08-14): 0 (no noise) at 0%
 *   → rises to kMaxAmount at 50% → falls back to kMinAmount at 100%. 50% is the
 *   loudest noise; both ends are quiet. kMaxAmount is deliberately short of full
 *   because at full equal-power blend the tone gain is exactly 0 and the chord
 *   vanishes into pure noise — the voice must stay present at every position.
 * - Noise attack, lower vs upper half:
 *   - 0–50%: short attack — noise is essentially immediate at the note onset
 *     (percussive chiff; the old constant-blend behavior, rescoped to the bottom
 *     half).
 *   - 50→100%: a slow attack ramps in, its time scaling with knob position (none
 *     at 50%, a little at 60%, a lot at 100%), so the noise swells in later and
 *     later after the transient.
 *
 * So the two halves are distinct characters, not two amounts of the same thing:
 * below 50% a loud immediate chiff that grows; above 50% a quieter, slower airy
 * bloom that fades away as the knob opens — quiet *and* late at 100%. Continuous
 * at 50% (both curves meet: peak amount, zero attack). Same "fold at 50%" idiom
 * as SummonerEnvelope.
 *
 * The attack envelope itself lives in the app (per-voice ramp); this maps the
 * knob to the equal-power blend amount and the attack time in seconds.
 */
struct SummonerNoiseFold
{
    /** @brief Noise parameters for one knob position. */
    struct Result
    {
        q15_t amount; ///< Equal-power blend amount — tent: 0 → kMaxAmount at 50% → kMinAmount at 100%
        float attack; ///< Noise attack time in seconds (0 across the lower half)
    };

    static constexpr float kMaxAttack = 1.5f; ///< Slowest noise swell (at 100%)

    /// Blend ceiling at the 50% peak — 70% of full. Equal-power at 70% leaves the
    /// tone at cos(0.7·π/2) ≈ 0.45, so the chord stays clearly audible under the
    /// noise (2026-08-14: full blend silenced the voice).
    static constexpr q15_t kMaxAmount = static_cast<q15_t>(Q15_MAX * 7 / 10);

    /// Blend floor at 100% — a faint 15%, not zero: the top of the knob is a
    /// quiet slow bloom rather than a second "off" position duplicating 0%.
    static constexpr q15_t kMinAmount = static_cast<q15_t>(Q15_MAX * 15 / 100);

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

        // Amount tent: 0 → kMaxAmount across the lower half, kMaxAmount →
        // kMinAmount across the upper. Peak (loudest noise) sits at 50%; never
        // full, so the voice survives under the noise at every position.
        if (x <= Q15_HALF)
        {
            const int32_t amt = static_cast<int32_t>(x) * kMaxAmount / Q15_HALF;
            r.amount = static_cast<q15_t>(amt > kMaxAmount ? kMaxAmount : amt);
        }
        else
        {
            const int32_t up = static_cast<int32_t>(x) - Q15_HALF;
            const int32_t span = Q15_MAX - Q15_HALF;
            const int32_t amt = kMaxAmount - (kMaxAmount - kMinAmount) * up / span;
            r.amount = static_cast<q15_t>(amt < kMinAmount ? kMinAmount : amt);
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
