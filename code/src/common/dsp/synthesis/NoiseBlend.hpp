/*
MIT License

Copyright (c) 2026 Sam Drilias

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <cmath>
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

constexpr float kNoiseBlendHalfPi = 1.57079632679489661923f;

/**
 * @brief Equal-power dry (oscillator) gain for a given blend amount.
 * @param blend Unipolar q15_t control in [0, Q15_MAX] (0 = fully dry, Q15_MAX = fully wet).
 * @return q15_t dry-signal gain, cosf(blend * pi/2) so dry_gain^2 + wet_gain^2 stays ~1.0
 *         across the sweep (Phase 1 decision 2026-07-21: chosen over the linear law, which
 *         has a perceptible ~3dB power dip at the midpoint — see NOISE-DRUMS.md / CHORD-GEN.md).
 */
inline q15_t NoiseBlendGainDry(q15_t blend)
{
    float b = q15_to_float(blend);
    return float_to_q15(cosf(b * kNoiseBlendHalfPi));
}

/**
 * @brief Equal-power wet (noise) gain for a given blend amount. See NoiseBlendGainDry().
 */
inline q15_t NoiseBlendGainWet(q15_t blend)
{
    float b = q15_to_float(blend);
    return float_to_q15(sinf(b * kNoiseBlendHalfPi));
}

/**
 * @brief Equal-power crossfade between an oscillator (dry) and noise (wet) sample.
 * @param dry Oscillator sample.
 * @param wet Noise sample.
 * @param blend Unipolar q15_t control in [0, Q15_MAX] (0 = fully dry, Q15_MAX = fully wet).
 * @return Blended q15_t sample with ~constant perceived loudness across the full sweep.
 */
inline q15_t NoiseBlend(q15_t dry, q15_t wet, q15_t blend)
{
    return q15_add(q15_mult(dry, NoiseBlendGainDry(blend)), q15_mult(wet, NoiseBlendGainWet(blend)));
}

} // namespace kastle2
