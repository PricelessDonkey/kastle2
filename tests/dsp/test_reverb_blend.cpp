#include "../harness.hpp"
#include "apps/Summoner/SummonerReverbBlend.hpp"

using namespace kastle2;

namespace
{
q15_t Knob(const float frac)
{
    return static_cast<q15_t>(frac * Q15_MAX + 0.5f);
}
}

TEST(SummonerReverbBlend_BottomIsFullyDry)
{
    // 0% must be a true dry signal: reverb bypassed in the mix.
    const auto r = SummonerReverbBlend::Compute(0);
    ASSERT_EQ(r.wet, 0);
    ASSERT_NEAR(r.decay, SummonerReverbBlend::kShortDecay, 1e-6f);
}

TEST(SummonerReverbBlend_BottomHalfRampsWetShortDecay)
{
    // 0 -> 50%: decay held short, wet rises monotonically 0 -> full.
    q15_t prev = -1;
    for (float f = 0.0f; f < 0.5f - 1e-3f; f += 0.05f)
    {
        const auto r = SummonerReverbBlend::Compute(Knob(f));
        ASSERT_NEAR(r.decay, SummonerReverbBlend::kShortDecay, 1e-6f);
        ASSERT_TRUE(r.wet >= prev); // monotonic non-decreasing
        prev = r.wet;
    }
}

TEST(SummonerReverbBlend_MidpointIsFullyWetShortDecay)
{
    // 50%: fully wet, shortest decay -> "short decay, very wet".
    const auto r = SummonerReverbBlend::Compute(Q15_HALF);
    ASSERT_EQ(r.wet, Q15_MAX);
    ASSERT_NEAR(r.decay, SummonerReverbBlend::kShortDecay, 1e-6f);
}

TEST(SummonerReverbBlend_TopHalfFullWetDecayGrows)
{
    // 50 -> 100%: wet pinned full, decay rises monotonically short -> long.
    float prev = -1.0f;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const auto r = SummonerReverbBlend::Compute(Knob(f));
        ASSERT_EQ(r.wet, Q15_MAX);
        ASSERT_TRUE(r.decay >= prev - 1e-6f); // monotonic non-decreasing
        prev = r.decay;
    }
}

TEST(SummonerReverbBlend_TopIsFullyWetLongDecay)
{
    // 100%: fully wet, longest decay -> "long decay, very wet".
    const auto r = SummonerReverbBlend::Compute(Q15_MAX);
    ASSERT_EQ(r.wet, Q15_MAX);
    ASSERT_NEAR(r.decay, SummonerReverbBlend::kLongDecay, 1e-6f);
}

TEST(SummonerReverbBlend_ContinuousAtMidpoint)
{
    // Smooth transition: just below and at 50% agree on wet(full)+decay(short).
    const auto below = SummonerReverbBlend::Compute(Q15_HALF - 1);
    const auto at = SummonerReverbBlend::Compute(Q15_HALF);
    ASSERT_NEAR(below.decay, at.decay, 1e-3f);
    ASSERT_TRUE(at.wet - below.wet < 8); // no jump in wet across the fold
}
