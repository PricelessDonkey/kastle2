#include "../harness.hpp"
#include "apps/Summoner/SummonerNoiseFold.hpp"

using namespace kastle2;

namespace
{
q15_t Knob(const float frac)
{
    return static_cast<q15_t>(frac * Q15_MAX + 0.5f);
}
}

TEST(SummonerNoiseFold_AmountZeroAtBottomMaxFromCenter)
{
    ASSERT_EQ(SummonerNoiseFold::Compute(0).amount, 0);
    ASSERT_EQ(SummonerNoiseFold::Compute(Q15_HALF).amount, Q15_MAX);
    ASSERT_EQ(SummonerNoiseFold::Compute(Q15_MAX).amount, Q15_MAX);
}

TEST(SummonerNoiseFold_AmountRampsUpLowerHalfThenHeld)
{
    // 0 -> 50%: amount rises monotonically. 50 -> 100%: held at max.
    q15_t prev = -1;
    for (float f = 0.0f; f < 0.5f - 1e-3f; f += 0.05f)
    {
        const q15_t a = SummonerNoiseFold::Compute(Knob(f)).amount;
        ASSERT_TRUE(a >= prev);
        prev = a;
    }
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        ASSERT_EQ(SummonerNoiseFold::Compute(Knob(f)).amount, Q15_MAX);
    }
}

TEST(SummonerNoiseFold_AttackInstantAcrossLowerHalf)
{
    // Lower half: noise is immediate (attack 0).
    for (float f = 0.0f; f <= 0.5f - 1e-3f; f += 0.05f)
    {
        ASSERT_NEAR(SummonerNoiseFold::Compute(Knob(f)).attack, 0.0f, 1e-6f);
    }
}

TEST(SummonerNoiseFold_AttackRampsInUpperHalf)
{
    // 50 -> 100%: attack rises monotonically from 0 to max.
    float prev = -1.0f;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const float a = SummonerNoiseFold::Compute(Knob(f)).attack;
        ASSERT_TRUE(a >= prev - 1e-6f);
        prev = a;
    }
    ASSERT_NEAR(SummonerNoiseFold::Compute(Q15_MAX).attack, SummonerNoiseFold::kMaxAttack, 1e-4f);
}

TEST(SummonerNoiseFold_ContinuousAtMidpoint)
{
    // No jump across the fold: full amount, ~zero attack on both sides of 50%.
    const auto below = SummonerNoiseFold::Compute(Q15_HALF - 1);
    const auto at = SummonerNoiseFold::Compute(Q15_HALF);
    ASSERT_TRUE(at.amount - below.amount < 8);
    ASSERT_NEAR(below.attack, at.attack, 1e-3f);
}
