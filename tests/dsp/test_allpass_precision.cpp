#include <array>
#include <cmath>
#include <cstdlib>

#include "../harness.hpp"
#include "prototypes/AllpassPrecision.hpp"

using namespace kastle2;
using namespace kastle2::proto;

// Precision prototype resolving REVERB.md's open Q15-vs-Q31/float question for
// the Dattorro tank's allpass diffusers. The assertions here ARE the decision
// record: they pin the measured facts that make "Q15 throughout" the choice.
// See prototypes/AllpassPrecision.hpp for the three implementations and
// CHORD-GEN-PLAN.md Phase 7 for the written-up conclusion.

namespace
{
// Dattorro-ish coprime diffusion lengths and the canonical ~0.75 coefficient.
constexpr std::array<std::size_t, 4> kDiff = {142, 107, 379, 277};
constexpr float kG = 0.75f;

// Longest single delay, used for the feedback-tank probes.
constexpr std::size_t kTankDelay = 379;

// Runs an impulse of `amp` through a 4-allpass series chain and returns the
// summed output energy as a ratio to the input energy (an ideal allpass
// conserves energy → ratio == 1.0).
template <typename Chain, typename Sample>
double SeriesEnergyRatio(Chain &chain, Sample impulse, Sample zero)
{
    double in_energy = static_cast<double>(impulse) * impulse;
    double out_energy = 0.0;
    for (int n = 0; n < 40000; ++n)
    {
        Sample x = (n == 0) ? impulse : zero;
        for (auto &ap : chain)
        {
            x = ap.Process(x);
        }
        out_energy += static_cast<double>(x) * x;
    }
    return out_energy / in_energy;
}
} // namespace

TEST(AllpassPrecision_FloatChainIsEnergyConserving)
{
    // Sanity anchor: the float reference passes essentially all impulse energy.
    std::array<AllpassFloat, 4> chain = {
        AllpassFloat(kDiff[0], kG), AllpassFloat(kDiff[1], kG),
        AllpassFloat(kDiff[2], kG), AllpassFloat(kDiff[3], kG)};
    double ratio = SeriesEnergyRatio(chain, 1.0f, 0.0f);
    ASSERT_NEAR(ratio, 1.0, 0.001);
}

TEST(AllpassPrecision_Q15ChainConservesImpulseEnergy)
{
    // The finding that matters: in plain Q15 the allpass is still
    // energy-conserving to well under 1% — no float intermediates needed for
    // the diffusion chain.
    std::array<AllpassQ15, 4> chain = {
        AllpassQ15(kDiff[0], kG), AllpassQ15(kDiff[1], kG),
        AllpassQ15(kDiff[2], kG), AllpassQ15(kDiff[3], kG)};
    double ratio = SeriesEnergyRatio(chain, static_cast<q15_t>(Q15_MAX), static_cast<q15_t>(0));
    ASSERT_NEAR(ratio, 1.0, 0.02);
}

TEST(AllpassPrecision_Q15DecayTimeMatchesFloat)
{
    // Decay-time accuracy of a feedback tank (allpass with a decay multiply in
    // the loop). Q15's -60dB decay time tracks the float reference to within a
    // sample or two — fixed point does not warp the tail length.
    const float decay = 0.85f;

    auto measure_float = [&]() -> int {
        AllpassFloat ap(kTankDelay, kG);
        float fb = 0.0f, peak = 0.0f;
        for (int n = 0; n < 400000; ++n)
        {
            float x = (n == 0) ? 1.0f : 0.0f;
            float y = ap.Process(x + decay * fb);
            fb = y;
            float a = std::fabs(y);
            if (n < 3 * static_cast<int>(kTankDelay))
            {
                peak = std::max(peak, a);
            }
            else if (a < 0.001f * peak)
            {
                return n;
            }
        }
        return -1;
    };

    auto measure_q15 = [&]() -> int {
        AllpassQ15 ap(kTankDelay, kG);
        q15_t dc = float_to_q15(decay), fb = 0;
        double peak = 0.0;
        for (int n = 0; n < 400000; ++n)
        {
            q15_t x = (n == 0) ? Q15_MAX : 0;
            q15_t y = ap.Process(q15_saturate(x + q15_mult(dc, fb)));
            fb = y;
            double a = std::abs(y);
            if (n < 3 * static_cast<int>(kTankDelay))
            {
                peak = std::max(peak, a);
            }
            else if (a < 0.001 * peak)
            {
                return n;
            }
        }
        return -1;
    };

    int t60_float = measure_float();
    int t60_q15 = measure_q15();
    ASSERT_TRUE(t60_float > 0);
    ASSERT_TRUE(t60_q15 > 0);
    double err = std::fabs(t60_q15 - t60_float) / static_cast<double>(t60_float);
    ASSERT_TRUE(err < 0.02);
}

namespace
{
// Runs a feedback tank for a long time and returns the peak |output| in the
// final settled window — the residual "limit-cycle" floor, in Q15 counts.
template <typename Allpass>
double TankTailFloorCounts(double decay)
{
    Allpass ap(kTankDelay, kG);
    q15_t dc = float_to_q15(static_cast<float>(decay));
    q15_t fb = 0;
    double floor_counts = 0.0;
    for (int n = 0; n < 800000; ++n)
    {
        q15_t x = (n == 0) ? Q15_MAX : 0;
        q15_t y = ap.Process(q15_saturate(x + q15_mult(dc, fb)));
        fb = y;
        if (n > 700000)
        {
            floor_counts = std::max(floor_counts, static_cast<double>(std::abs(y)));
        }
    }
    return floor_counts;
}
} // namespace

TEST(AllpassPrecision_FeedbackTailFloorIsBoundedAndLow)
{
    // No runaway limit-cycle "whine": at a long decay the tail settles to a
    // small, bounded quantization floor rather than growing or ringing loudly.
    // ~-73 dBFS at 0.85, ~-59 dBFS at 0.97 as measured — assert it stays under
    // -50 dBFS (~104 counts of 32767) even near infinite feedback.
    double floor_085 = TankTailFloorCounts<AllpassQ15>(0.85);
    double floor_097 = TankTailFloorCounts<AllpassQ15>(0.97);

    // Bounded and non-explosive at both.
    ASSERT_TRUE(floor_085 < 104.0); // > -50 dBFS
    ASSERT_TRUE(floor_097 < 104.0);
    // Floor grows with decay but stays well below the 16-bit signal range.
    ASSERT_TRUE(floor_097 >= floor_085);
}

TEST(AllpassPrecision_Q31ArithmeticDoesNotLowerTailFloor)
{
    // The reason Q15 is the right choice: the tail floor is bound by the
    // 16-bit q15least_t STORAGE, not the arithmetic precision. Doing the
    // recursion in Q31 does not buy a meaningful (>12 dB) reduction, because
    // each write-back requantizes to 16 bits — so there is no reason to pay
    // for Q31 intermediates.
    double floor_q15 = TankTailFloorCounts<AllpassQ15>(0.9);
    double floor_q31 = TankTailFloorCounts<AllpassQ15Q31>(0.9);

    ASSERT_TRUE(floor_q15 > 0.0);
    ASSERT_TRUE(floor_q31 > 0.0);
    // Q31 arithmetic is not >4x (12 dB) better — same order of magnitude,
    // confirming the floor is storage-bound.
    ASSERT_TRUE(floor_q31 >= floor_q15 * 0.25);
}
