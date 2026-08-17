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

TEST(SummonerNoiseFold_AmountTentPeaksAtCenter)
{
    // 50% is the loudest noise; both ends are quiet (2026-08-14 revision).
    ASSERT_EQ(SummonerNoiseFold::Compute(0).amount, 0);
    ASSERT_EQ(SummonerNoiseFold::Compute(Q15_HALF).amount, SummonerNoiseFold::kMaxAmount);
    ASSERT_EQ(SummonerNoiseFold::Compute(Q15_MAX).amount, SummonerNoiseFold::kMinAmount);
    ASSERT_TRUE(SummonerNoiseFold::kMinAmount < SummonerNoiseFold::kMaxAmount);
}

TEST(SummonerNoiseFold_AmountNeverSilencesTheVoice)
{
    // The blend ceiling must stay short of full so the equal-power tone gain
    // never reaches 0 — at full blend the chord disappeared into pure noise.
    ASSERT_TRUE(SummonerNoiseFold::kMaxAmount < Q15_MAX);
    for (float f = 0.0f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        ASSERT_TRUE(SummonerNoiseFold::Compute(Knob(f)).amount <= SummonerNoiseFold::kMaxAmount);
    }
}

TEST(SummonerNoiseFold_AmountRisesThenFallsAcrossTheFold)
{
    // 0 -> 50%: amount rises monotonically to the peak.
    q15_t prev = -1;
    for (float f = 0.0f; f < 0.5f - 1e-3f; f += 0.05f)
    {
        const q15_t a = SummonerNoiseFold::Compute(Knob(f)).amount;
        ASSERT_TRUE(a >= prev);
        prev = a;
    }
    // 50 -> 100%: amount falls monotonically back to the floor.
    prev = Q15_MAX;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const q15_t a = SummonerNoiseFold::Compute(Knob(f)).amount;
        ASSERT_TRUE(a <= prev);
        ASSERT_TRUE(a >= SummonerNoiseFold::kMinAmount);
        prev = a;
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
    // No jump across the fold: both tent legs meet at the peak with ~zero attack.
    const auto below = SummonerNoiseFold::Compute(Q15_HALF - 1);
    const auto at = SummonerNoiseFold::Compute(Q15_HALF);
    const auto above = SummonerNoiseFold::Compute(Q15_HALF + 1);
    ASSERT_TRUE(at.amount - above.amount < 8);
    ASSERT_TRUE(at.amount - below.amount < 8);
    ASSERT_NEAR(below.attack, at.attack, 1e-3f);
}
