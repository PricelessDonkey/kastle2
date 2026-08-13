#include <cmath>
#include <cstdlib>

#include "../harness.hpp"
#include "common/dsp/effects/ShimmerReverb.hpp"

using namespace kastle2;

// ShimmerReverb: Dattorro plate + granular pitch-shift feedback. These tests
// pin the three properties the plan's Phase 7 task calls for — RMS decay
// tracks SetDecay(), shimmer-off is a plain (fully decaying) plate, and
// shimmer at 100% stays bounded (no runaway) rather than exploding.

namespace
{
constexpr float kSampleRate = 44000.0f;

// Fires one full-scale impulse, runs `total` samples, and returns the RMS of
// the stereo output over the last `window` samples (the settled tail).
double TailRms(ShimmerReverb &rev, int total, int window)
{
    double acc = 0.0;
    int counted = 0;
    for (int n = 0; n < total; ++n)
    {
        q15_t x = (n == 0) ? Q15_MAX : 0;
        ShimmerReverb::Output o = rev.Process(x);
        if (n >= total - window)
        {
            acc += static_cast<double>(o.left) * o.left + static_cast<double>(o.right) * o.right;
            counted += 2;
        }
    }
    return std::sqrt(acc / counted);
}

// Peak |output| over the whole run — used to prove boundedness.
double PeakAbs(ShimmerReverb &rev, int total)
{
    double peak = 0.0;
    for (int n = 0; n < total; ++n)
    {
        q15_t x = (n == 0) ? Q15_MAX : 0;
        ShimmerReverb::Output o = rev.Process(x);
        peak = std::max(peak, static_cast<double>(std::abs(o.left)));
        peak = std::max(peak, static_cast<double>(std::abs(o.right)));
    }
    return peak;
}
} // namespace

TEST(ShimmerReverb_LongerDecayRingsLonger)
{
    // The tail energy in a fixed late window grows with the decay parameter.
    ShimmerReverb low;
    low.Init(kSampleRate);
    low.SetShimmer(0);
    low.SetDecay(0.5f);
    double rms_low = TailRms(low, 40000, 4000);

    ShimmerReverb high;
    high.Init(kSampleRate);
    high.SetShimmer(0);
    high.SetDecay(0.9f);
    double rms_high = TailRms(high, 40000, 4000);

    ASSERT_TRUE(rms_high > rms_low * 2.0);
}

TEST(ShimmerReverb_ProducesAWetTail)
{
    // Sanity: an impulse actually excites a diffuse tail (not silence).
    ShimmerReverb rev;
    rev.Init(kSampleRate);
    rev.SetShimmer(0);
    rev.SetDecay(0.85f);
    // Energy in an early-ish window, after diffusion has spread the impulse.
    double early = TailRms(rev, 8000, 4000);
    ASSERT_TRUE(early > 1.0);
}

TEST(ShimmerReverb_ShimmerOffIsAPlainDecayingPlate)
{
    // Shimmer = 0 → clean plate: the tail must decay away, so a late window is
    // far quieter than an early one (and bounded, never growing).
    ShimmerReverb rev;
    rev.Init(kSampleRate);
    rev.SetShimmer(0);
    rev.SetDecay(0.8f);

    double early = 0.0, late = 0.0;
    int early_n = 0, late_n = 0;
    for (int n = 0; n < 120000; ++n)
    {
        q15_t x = (n == 0) ? Q15_MAX : 0;
        ShimmerReverb::Output o = rev.Process(x);
        double e = static_cast<double>(o.left) * o.left + static_cast<double>(o.right) * o.right;
        if (n >= 4000 && n < 8000)
        {
            early += e;
            early_n += 2;
        }
        else if (n >= 116000)
        {
            late += e;
            late_n += 2;
        }
    }
    double rms_early = std::sqrt(early / early_n);
    double rms_late = std::sqrt(late / late_n);
    ASSERT_TRUE(rms_early > 1.0);
    ASSERT_TRUE(rms_late < rms_early * 0.1); // decayed to <10% of early energy
}

TEST(ShimmerReverb_FullShimmerStaysBounded)
{
    // Shimmer = 100% with a high decay is the "infinite shimmer" case. It must
    // not run away: output stays within the Q15 range across a long run.
    ShimmerReverb rev;
    rev.Init(kSampleRate);
    rev.SetShimmer(Q15_MAX);
    rev.SetDecay(0.9f);
    rev.SetInterval(ShimmerReverb::Interval::OCTAVE_UP);

    double peak = PeakAbs(rev, 400000);
    ASSERT_TRUE(peak <= static_cast<double>(Q15_MAX));
    ASSERT_TRUE(peak > 0.0); // it does sustain something
}

TEST(ShimmerReverb_ResetSilencesTail)
{
    // After Reset() the tail is gone: a fresh run produces the same first
    // output as a freshly-inited instance would.
    ShimmerReverb rev;
    rev.Init(kSampleRate);
    rev.SetShimmer(Q15_MAX / 2);
    rev.SetDecay(0.9f);
    for (int n = 0; n < 20000; ++n)
    {
        rev.Process((n == 0) ? Q15_MAX : 0);
    }
    rev.Reset();
    ShimmerReverb::Output o = rev.Process(0);
    ASSERT_EQ(o.left, 0);
    ASSERT_EQ(o.right, 0);
}
