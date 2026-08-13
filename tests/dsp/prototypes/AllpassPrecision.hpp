#pragma once

/**
 * @file AllpassPrecision.hpp
 * @brief Precision prototype for the Dattorro plate tank's allpass diffusers.
 * @author Summoner (CHORD-GEN) build session
 * @date 2026-08-13
 *
 * Resolves REVERB.md's open question — "Can we use Q15 fixed-point throughout
 * the pitch shifter / tank, or do the recursions need float / Q31
 * intermediates?" — by pitting three implementations of a Schroeder/Dattorro
 * allpass diffuser against each other on the properties that actually matter
 * for a reverb tank:
 *
 *   - impulse-response energy conservation (an allpass must pass all energy),
 *   - decay-time accuracy of a feedback tank vs the float reference,
 *   - the residual tail floor (fixed-point limit-cycle "whine").
 *
 * Finding (see tests/dsp/test_allpass_precision.cpp for the assertions that
 * pin it, and CHORD-GEN-PLAN.md Phase 7 for the decision record): **Q15
 * throughout is adequate.** The allpass is energy-conserving to ~0.02% in
 * Q15. The feedback tail settles to a bounded quantization floor
 * (~-73 dBFS at decay 0.85, rising gently to ~-59 dBFS at 0.97) that is set
 * by the 16-bit q15least_t *storage* of the delay line — doing the arithmetic
 * in Q31 does NOT lower it, because every write-back requantizes to 16 bits.
 * So there is no reason to pay for Q31 recursions or float: store buffers as
 * q15least_t (keeps the ~38KB budget) and do the math in q15_t.
 *
 * These are throwaway reference implementations for the measurement, not the
 * shipping ShimmerReverb — that lands in common/dsp/effects/ next. Kept as a
 * prototype (like NoiseBlendCrossfade was) so the decision stays reproducible.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/math/qmath.hpp"

namespace kastle2::proto
{

/** @brief Float reference Schroeder allpass — the ground truth to compare against. */
class AllpassFloat
{
public:
    AllpassFloat(std::size_t delay, float g) : buf_(delay, 0.0f), g_(g) {}

    float Process(float x)
    {
        float delayed = buf_[pos_];
        float w = x + g_ * delayed;
        float y = delayed - g_ * w;
        buf_[pos_] = w;
        pos_ = (pos_ + 1) % buf_.size();
        return y;
    }

private:
    std::vector<float> buf_;
    std::size_t pos_ = 0;
    float g_;
};

/**
 * @brief Q15-storage, Q15-arithmetic allpass — the candidate we want to ship.
 *
 * Delay line stored as q15least_t (true 16-bit) per the RAM budget; the
 * recursion runs in q15_t (32-bit-backed) with the library's saturating
 * truncating q15_mult.
 */
class AllpassQ15
{
public:
    AllpassQ15(std::size_t delay, float g) : buf_(delay, 0), g_(float_to_q15(g)) {}

    q15_t Process(q15_t x)
    {
        q15_t delayed = buf_[pos_];
        q15_t w = q15_saturate(x + q15_mult(g_, delayed));
        q15_t y = q15_saturate(delayed - q15_mult(g_, w));
        buf_[pos_] = static_cast<q15least_t>(w);
        pos_ = (pos_ + 1) % buf_.size();
        return y;
    }

private:
    std::vector<q15least_t> buf_;
    std::size_t pos_ = 0;
    q15_t g_;
};

/**
 * @brief Q15-storage but Q31 arithmetic — the "does higher precision help?"
 *        control. Buffer is still 16-bit; only the intermediate math is Q31.
 *        Included to demonstrate the tail floor is storage-bound, not
 *        arithmetic-bound.
 */
class AllpassQ15Q31
{
public:
    AllpassQ15Q31(std::size_t delay, float g) : buf_(delay, 0), g_(q15_to_q31(float_to_q15(g))) {}

    q15_t Process(q15_t x)
    {
        q31_t delayed = q15_to_q31(buf_[pos_]);
        q31_t w = q31_add(q15_to_q31(x), q31_mult(g_, delayed));
        q31_t y = q31_sub(delayed, q31_mult(g_, w));
        buf_[pos_] = static_cast<q15least_t>(q31_to_q15(w));
        pos_ = (pos_ + 1) % buf_.size();
        return q31_to_q15(y);
    }

private:
    std::vector<q15least_t> buf_;
    std::size_t pos_ = 0;
    q31_t g_;
};

} // namespace kastle2::proto
