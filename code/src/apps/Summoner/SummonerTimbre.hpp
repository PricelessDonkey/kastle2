#pragma once

#include <cmath>
#include "common/config.hpp"
#include "common/dsp/math/math_utils.hpp"
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerTimbre
 * @ingroup apps
 * @brief Pure helpers for Summoner's filter envelope modulation (Phase 6).
 * @author sam
 * @date 2026-08-06
 *
 * Filter env amount = BANK+POT_2, bipolar with a center-off deadzone
 * (CHORD-GEN.md Timbre modulation): right of center each chord hit opens the
 * filter and decays with the notes, left of center hits darken and the tail
 * brightens. The modulation source is the summed voice envelope — the same
 * signal ENV_OUT carries. Kept hardware-free so the mapping is host-testable.
 */
class SummonerTimbre
{
public:
    /** @brief Cutoff floor — fully closed, but the Svf stays stable. */
    static constexpr float kMinCutoffHz = 30.0f;

    /** @brief Cutoff ceiling — comfortably under the Svf's sample_rate/3 limit. */
    static constexpr float kMaxCutoffHz = 12000.0f;

    /**
     * @brief Env-mod swing at full amount and full-scale envelope, in octaves.
     *
     * The envelope source keeps the audio mix's ÷4 headroom (one voice peaks
     * at ~25% of q15 full scale, a sustained chord at ~60%), so the practical
     * swing at full amount is ~1.5 octaves per voice / ~3.6 for a full chord.
     */
    static constexpr float kEnvModOctaves = 6.0f;

    /**
     * @brief Maps the bipolar env-amount pot to -1..+1 (center = off).
     * @param pot_val Pot value 0..POT_MAX; the FancyPot deadzone plateaus at pot(0.5f).
     * @return Signed amount in [-1, 1]; exactly 0 across the deadzone plateau.
     */
    static constexpr float EnvAmountFromPot(const int32_t pot_val)
    {
        constexpr int32_t center = pot(0.5f);
        float amount = static_cast<float>(pot_val - center) / static_cast<float>(center);
        if (amount < -1.0f)
        {
            amount = -1.0f;
        }
        if (amount > 1.0f)
        {
            amount = 1.0f;
        }
        return amount;
    }

    /**
     * @brief Applies envelope modulation to a base cutoff, in pitch (octave) space.
     * @param base_hz Base cutoff from the cutoff knob (Hz).
     * @param env_amount Bipolar amount from EnvAmountFromPot(), -1..+1.
     * @param env_mix Summed voice envelope (q15, the ENV_OUT signal).
     * @return Modulated cutoff, clamped to [kMinCutoffHz, kMaxCutoffHz].
     */
    static float ModulatedCutoffHz(const float base_hz, const float env_amount, const q15_t env_mix)
    {
        float cutoff = base_hz;
        if (env_amount != 0.0f && env_mix > 0)
        {
            cutoff *= std::exp2(env_amount * kEnvModOctaves * q15_to_float(env_mix));
        }
        if (cutoff < kMinCutoffHz)
        {
            cutoff = kMinCutoffHz;
        }
        if (cutoff > kMaxCutoffHz)
        {
            cutoff = kMaxCutoffHz;
        }
        return cutoff;
    }
};
}
