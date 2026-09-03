#pragma once

#include <cmath>
#include <cstdint>
#include <climits>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerNoiseTimbre
 * @ingroup apps
 * @brief Folded noise *character* knob map (SHIFT+BANK+POT_1): spectral tilt
 *        below 50%, dust density coupled to bandpass resonance above it. Pure
 *        logic, host-tested (see test_summoner_noise_timbre).
 * @author Summoner (CHORD-GEN) Phase 15
 * @date 2026-09-03
 *
 * Replaces the retired detune-spread slot (CHORD-GEN.md Noise timbre knob §).
 * The two noise knobs now split as POT_6 = "how much and when", POT_1 = "what
 * kind". Folds at 50%, same idiom as SummonerEnvelope / SummonerNoiseFold:
 *
 * - **0 → 50% — spectral tilt.** A one-pole lowpass output is crossfaded against
 *   the flat noise: full lowpass at 0% (brown, rumble/body), roughly half at 25%
 *   (pink, the pad wash), flat at 50% (white). The lowpass path carries a make-up
 *   gain because a one-pole at a low cutoff throws away most of white noise's
 *   power — without it "brown" is just "quieter", which reads as a broken knob.
 * - **50 → 100% — dust density coupled to resonance.** Density falls *while* Q
 *   rises, as one gesture, because the two reinforce each other: dense noise
 *   through a high-Q bandpass smears into vague hiss, whereas sparse impulses let
 *   the filter ring audibly on each hit (the plucked-string principle). At the top
 *   the noise is sparse pitched plinks tracking the chord root.
 *
 * Continuous at 50%: the tilt path ends flat and the dust path starts at full
 * density (density 1.0 *is* white noise by definition), zero resonance and a fully
 * per-voice mix, so both halves meet at plain white — the current as-built sound.
 *
 * **Dust by threshold.** Rather than drawing a second random number to decide
 * whether an impulse fires, the app keeps a noise sample only when it exceeds a
 * threshold. Free (one comparison), and it makes dust a change to the noise
 * *source* rather than an added stage. For noise uniform on [-1, 1] with
 * threshold t = 1 - d, the surviving output's mean square is (1 - (1-d)^3)/3
 * against white's 1/3, so exact amplitude compensation is 1/sqrt(1 - (1-d)^3) —
 * closed form, 1.0 at full density, and capped (see kMaxDustGain) so the sparsest
 * settings stay honest instead of clipping.
 *
 * **Shared vs per-voice.** shared_mix crossfades the four independent per-voice
 * noise streams toward one shared dusted+resonant stream across the upper half.
 * The upper half is therefore correlated/mono (focused pings) where the lower half
 * is wide (a four-stream wash) — the deliberate trade recorded in CHORD-GEN.md.
 */
struct SummonerNoiseTimbre
{
    /** @brief Noise character parameters for one knob position. */
    struct Result
    {
        q15_t tilt;         ///< Crossfade toward the lowpass output: Q15_MAX = brown, 0 = flat
        q15_t dust_thresh;  ///< |noise| below this is zeroed; 0 = full density (white)
        int32_t dust_gain;  ///< Q15 make-up multiplier for the surviving impulses (>= Q15_MAX)
        float resonance;    ///< Shared bandpass resonance, 0 at/below the fold
        q15_t shared_mix;   ///< 0 = fully per-voice/independent, Q15_MAX = fully shared stream
    };

    /// One-pole lowpass cutoff for the tilt path. Low enough that 0% reads as
    /// rumble rather than "slightly dull", high enough that the make-up gain
    /// stays modest.
    static constexpr float kTiltCutoffHz = 900.0f;

    /// Make-up gain on the lowpass path, as a plain integer multiplier. A
    /// one-pole's RMS gain on white noise is sqrt(a / (2 - a)) with a the
    /// smoothing coefficient; at 900 Hz / 44 kHz that is ~0.25, so 4x restores the
    /// level. Pinned by the RMS test.
    ///
    /// Integer rather than a Q15 fraction on purpose: as Q15 the intermediate
    /// (lp * 4 * Q15_MAX) overflows int32 at full scale, and the audio path must
    /// not reach for 64-bit math on an RP2040. Multiply then clamp instead.
    static constexpr int32_t kLowpassMakeup = 4;

    /// Sparsest density at 100% — 1% of samples survive.
    ///
    /// Measured 2026-09-03, and it corrected the design: threshold dust keeps the
    /// *largest* samples, so survivors are already near full scale and make-up
    /// gain past ~2x buys nothing but clipping. The achievable level ceiling is
    /// therefore ~sqrt(density) no matter what the gain says, which makes density
    /// itself the only real loudness lever at the sparse end. 0.004 measured ~19 dB
    /// below white — too faint to carry the top of the knob — so the floor was
    /// raised to 0.01 (~15 dB down) and the rest is left to the resonator, whose
    /// gain at kMaxResonance is substantial.
    static constexpr float kMinDensity = 0.01f;

    /// Ceiling on the dust make-up gain. Compensation is genuinely effective in
    /// the moderate range (~0.1–1.0 density) where survivors are not yet pinned to
    /// full scale; beyond that it only clips, so the cap is set where it stops
    /// helping rather than at the arithmetic ideal.
    static constexpr int32_t kMaxDustGain = 2 * Q15_MAX;

    /// Resonance at 100%. Svf is stable to 1.0; stopping short keeps the ring
    /// musical rather than a self-oscillating whistle.
    static constexpr float kMaxResonance = 0.92f;

    /// The app applies the dust gain as (sample * dust_gain) >> 15 in the audio
    /// path, so the product must stay inside int32 without reaching for 64-bit
    /// math. At the cap this is 32767 * 65534 — it fits, but only just, so raising
    /// kMaxDustGain is not a free change.
    static_assert(static_cast<int64_t>(kMaxDustGain) * Q15_MAX <= INT32_MAX,
                  "dust gain product must not overflow int32 in the audio path");

    /**
     * @brief One-pole smoothing coefficient for the tilt lowpass, as q15.
     * @param sample_rate Audio sample rate in Hz.
     */
    static q15_t TiltCoef(float sample_rate)
    {
        const float a = 1.0f - std::exp(-2.0f * 3.14159265358979f * kTiltCutoffHz / sample_rate);
        return float_to_q15(a);
    }

    /**
     * @brief Map a knob value (0..Q15_MAX) to the noise character parameters.
     * @param x Knob position as q15 (use pot_to_q15 on the combo slot value).
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
            // Lower half: tilt only. Full lowpass at 0%, flat at the fold.
            const int32_t up = static_cast<int32_t>(Q15_HALF - x);
            r.tilt = static_cast<q15_t>(up * Q15_MAX / Q15_HALF);
            r.dust_thresh = 0;
            r.dust_gain = Q15_MAX;
            r.resonance = 0.0f;
            r.shared_mix = 0;
            return r;
        }

        // Upper half: flat noise, thinning density, rising resonance, crossfading
        // to the shared stream. All three move together as one gesture.
        const float u = static_cast<float>(x - Q15_HALF) / static_cast<float>(Q15_MAX - Q15_HALF);

        r.tilt = 0;
        r.resonance = kMaxResonance * u;
        r.shared_mix = static_cast<q15_t>(u * static_cast<float>(Q15_MAX));

        // Density falls geometrically 1.0 -> kMinDensity, so the sparse end gets
        // real knob travel instead of collapsing in the last few percent.
        const float d = std::pow(kMinDensity, u);
        r.dust_thresh = float_to_q15(1.0f - d);

        // Exact uniform-noise compensation, capped.
        const float surviving = 1.0f - (1.0f - d) * (1.0f - d) * (1.0f - d);
        const float g = (surviving > 0.0f) ? (1.0f / std::sqrt(surviving)) : 1.0f;
        const int32_t gq = static_cast<int32_t>(g * static_cast<float>(Q15_MAX));
        r.dust_gain = gq > kMaxDustGain ? kMaxDustGain : (gq < Q15_MAX ? Q15_MAX : gq);
        return r;
    }
};

} // namespace kastle2
