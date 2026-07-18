#pragma once

// Prototype only — not part of the shared DSP library. Sketches the two
// candidate blend laws from CHORD-GEN.md's "Noise blend parameter" section
// (`/Users/sam/kastle2/CHORD-GEN.md`) so the linear-vs-equal-power decision
// can be made from measured behavior instead of guessing. If a law is
// chosen, promote it into `common/dsp/synthesis/` for real; don't wire this
// header into an app directly.
//
// `blend` is a unipolar q15_t control in [0, Q15_MAX] representing 0.0-1.0
// (0 = fully dry/oscillator, Q15_MAX = fully wet/noise), matching how other
// "amount" knobs (e.g. OscillatorQ15's amplitude_) are represented in this
// codebase.

#include <cmath>
#include "common/dsp/math/qmath.hpp"

namespace kastle2::prototype
{

constexpr float kHalfPi = 1.57079632679489661923f;

// Cheap option: dry_gain + wet_gain always sum to 1.0, but their squared
// sum (proportional to perceived loudness) dips to 0.5 at the midpoint —
// a perceptible ~3dB volume dip while sweeping through the blend range.
inline q15_t LinearBlendGainDry(q15_t blend) { return q15_inv(blend); }
inline q15_t LinearBlendGainWet(q15_t blend) { return blend; }

inline q15_t LinearBlend(q15_t dry, q15_t wet, q15_t blend)
{
    return q15_add(q15_mult(dry, LinearBlendGainDry(blend)), q15_mult(wet, LinearBlendGainWet(blend)));
}

// Smoother option: quarter-cosine/sine law. Squared gains sum to ~1.0
// across the whole range, so sweeping the blend knob shouldn't change
// perceived loudness. Costs a sin/cos per update (fine at ~114Hz CV rate,
// not something to call per-sample) — prototype uses libm float
// trig/sqrt since the question here is "which law", not fixed-point cost;
// a real implementation would need a lookup table or fixed-point sqrt.
inline q15_t EqualPowerBlendGainDry(q15_t blend)
{
    float b = q15_to_float(blend);
    return float_to_q15(cosf(b * kHalfPi));
}

inline q15_t EqualPowerBlendGainWet(q15_t blend)
{
    float b = q15_to_float(blend);
    return float_to_q15(sinf(b * kHalfPi));
}

inline q15_t EqualPowerBlend(q15_t dry, q15_t wet, q15_t blend)
{
    return q15_add(q15_mult(dry, EqualPowerBlendGainDry(blend)), q15_mult(wet, EqualPowerBlendGainWet(blend)));
}

} // namespace kastle2::prototype
