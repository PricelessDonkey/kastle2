#include "../harness.hpp"
#include "apps/Summoner/SummonerEnvelope.hpp"

using namespace kastle2;

namespace
{
q15_t Knob(const float frac)
{
    return static_cast<q15_t>(frac * Q15_MAX + 0.5f);
}
}

TEST(SummonerEnvelope_DecayIsShortestAtCenter)
{
    // 50% is the tightest pluck; both ends are the longest tail.
    const auto center = SummonerEnvelope::Compute(Q15_HALF);
    const auto low = SummonerEnvelope::Compute(0);
    const auto high = SummonerEnvelope::Compute(Q15_MAX);
    ASSERT_NEAR(center.decay, SummonerEnvelope::kMinDecay, 1e-4f);
    ASSERT_NEAR(low.decay, SummonerEnvelope::kMaxDecay, 1e-4f);
    ASSERT_NEAR(high.decay, SummonerEnvelope::kMaxDecay, 1e-4f);
}

TEST(SummonerEnvelope_DecayVShapeMonotonicEachHalf)
{
    // Lower half: decay decreases toward center. Upper half: increases away.
    float prev = 1e9f;
    for (float f = 0.0f; f <= 0.5f + 1e-4f; f += 0.05f)
    {
        const auto r = SummonerEnvelope::Compute(Knob(f));
        ASSERT_TRUE(r.decay <= prev + 1e-4f); // non-increasing toward center
        prev = r.decay;
    }
    prev = -1.0f;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const auto r = SummonerEnvelope::Compute(Knob(f));
        ASSERT_TRUE(r.decay >= prev - 1e-4f); // non-decreasing away from center
        prev = r.decay;
    }
}

TEST(SummonerEnvelope_AttackInstantAcrossLowerHalf)
{
    // 0 -> 50%: attack is always the minimum (instant), never ramps.
    // Stop just short of 50%: Knob(0.5) can round one q15 tick past Q15_HALF
    // into the upper branch (verified continuous by the midpoint test).
    for (float f = 0.0f; f <= 0.5f - 1e-3f; f += 0.05f)
    {
        const auto r = SummonerEnvelope::Compute(Knob(f));
        ASSERT_NEAR(r.attack, SummonerEnvelope::kMinAttack, 1e-6f);
    }
}

TEST(SummonerEnvelope_AttackRampsInUpperHalf)
{
    // 50 -> 100%: attack rises monotonically from minimum to maximum.
    float prev = -1.0f;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const auto r = SummonerEnvelope::Compute(Knob(f));
        ASSERT_TRUE(r.attack >= prev - 1e-6f);
        prev = r.attack;
    }
    const auto top = SummonerEnvelope::Compute(Q15_MAX);
    ASSERT_NEAR(top.attack, SummonerEnvelope::kMaxAttack, 1e-4f);
}

TEST(SummonerEnvelope_ContinuousAtMidpoint)
{
    // No jump across the fold: just-below and at 50% agree on both times.
    const auto below = SummonerEnvelope::Compute(Q15_HALF - 1);
    const auto at = SummonerEnvelope::Compute(Q15_HALF);
    ASSERT_NEAR(below.decay, at.decay, 1e-3f);
    ASSERT_NEAR(below.attack, at.attack, 1e-3f);
}
