#include "../harness.hpp"
#include "apps/Summoner/SummonerLfoShape.hpp"

using namespace kastle2;

namespace
{
constexpr int32_t kTri = SummonerLfoShape::kTriMax;

int32_t Pot(const float frac)
{
    return static_cast<int32_t>(frac * 4095.0f + 0.5f);
}

WhiteNoise MakeNoise(const uint32_t seed)
{
    WhiteNoise n;
    n.Seed(seed);
    return n;
}
}

TEST(SummonerLfoShape_StockZone_IsBitIdentical)
{
    // 20–80% must pass the triangle through untouched, every buffer.
    SummonerLfoShape shape;
    WhiteNoise noise = MakeNoise(1);
    for (int32_t p = SummonerLfoShape::kLowZoneEnd; p <= SummonerLfoShape::kHighZoneStart; p += 137)
    {
        for (int32_t tri = 0; tri <= kTri; tri += 111)
        {
            const bool wrap = (tri % 2 == 0);
            ASSERT_EQ(shape.Process(tri, wrap, p, noise), tri);
        }
    }
}

TEST(SummonerLfoShape_Amounts_RampMonotonicToFullAtExtremes)
{
    // Wander: 0 at/above 20%, full (1.0) at 0%, monotonic in between.
    ASSERT_NEAR(SummonerLfoShape::WanderAmount(SummonerLfoShape::kLowZoneEnd), 0.0f, 1e-6f);
    ASSERT_NEAR(SummonerLfoShape::WanderAmount(0), 1.0f, 1e-6f);
    ASSERT_NEAR(SummonerLfoShape::WanderAmount(Pot(0.9f)), 0.0f, 1e-6f);
    float prev = -1.0f;
    for (int32_t p = SummonerLfoShape::kLowZoneEnd; p >= 0; p -= 20)
    {
        const float a = SummonerLfoShape::WanderAmount(p);
        ASSERT_TRUE(a >= prev - 1e-6f);
        prev = a;
    }

    // Sample & hold: 0 at/below 80%, full at 100%, monotonic.
    ASSERT_NEAR(SummonerLfoShape::SampleHoldAmount(SummonerLfoShape::kHighZoneStart), 0.0f, 1e-6f);
    ASSERT_NEAR(SummonerLfoShape::SampleHoldAmount(4095), 1.0f, 1e-6f);
    ASSERT_NEAR(SummonerLfoShape::SampleHoldAmount(Pot(0.1f)), 0.0f, 1e-6f);
    prev = -1.0f;
    for (int32_t p = SummonerLfoShape::kHighZoneStart; p <= 4095; p += 20)
    {
        const float a = SummonerLfoShape::SampleHoldAmount(p);
        ASSERT_TRUE(a >= prev - 1e-6f);
        prev = a;
    }
}

TEST(SummonerLfoShape_EffectiveRatePot_PinsAtBoundaries)
{
    // Inside the stock band: unchanged.
    for (int32_t p = SummonerLfoShape::kLowZoneEnd; p <= SummonerLfoShape::kHighZoneStart; p += 200)
    {
        ASSERT_EQ(SummonerLfoShape::EffectiveRatePot(p), p);
    }
    // Below/above: pinned to the boundary, never past it.
    ASSERT_EQ(SummonerLfoShape::EffectiveRatePot(0), SummonerLfoShape::kLowZoneEnd);
    ASSERT_EQ(SummonerLfoShape::EffectiveRatePot(Pot(0.1f)), SummonerLfoShape::kLowZoneEnd);
    ASSERT_EQ(SummonerLfoShape::EffectiveRatePot(4095), SummonerLfoShape::kHighZoneStart);
    ASSERT_EQ(SummonerLfoShape::EffectiveRatePot(Pot(0.95f)), SummonerLfoShape::kHighZoneStart);
}

TEST(SummonerLfoShape_SampleHold_RedrawsOncePerWrapNotPerBuffer)
{
    // Full S&H (pot = 100%): output holds flat between wraps and only changes
    // on a wrapped buffer.
    SummonerLfoShape shape;
    WhiteNoise noise = MakeNoise(7);
    const int32_t pot = 4095;

    // First wrap establishes a held value.
    shape.Process(500, true, pot, noise);
    const int32_t held = shape.Process(500, false, pot, noise);
    // Many non-wrap buffers: value must not move even as raw_tri changes.
    for (int32_t k = 0; k < 200; k++)
    {
        ASSERT_EQ(shape.Process(k % (kTri + 1), false, pot, noise), held);
    }
    // A wrap draws a new value (statistically different across several wraps).
    int changes = 0;
    int32_t last = held;
    for (int32_t w = 0; w < 8; w++)
    {
        const int32_t v = shape.Process(500, true, pot, noise);
        if (v != last)
        {
            changes++;
        }
        last = v;
    }
    ASSERT_TRUE(changes >= 6); // re-rolls each wrap, not fixed per pattern
}

TEST(SummonerLfoShape_Wander_SlewsAndStaysInRange)
{
    // Full wander (pot = 0): output must remain a valid triangle value and the
    // slew must actually move toward fresh targets across cycles (not frozen).
    SummonerLfoShape shape;
    WhiteNoise noise = MakeNoise(3);
    const int32_t pot = 0;
    const int32_t period = 40;

    int distinct_outputs = 0;
    int32_t prev = -1;
    for (int32_t cyc = 0; cyc < 6; cyc++)
    {
        for (int32_t f = 0; f < period; f++)
        {
            const bool wrap = (f == 0);
            const int32_t out = shape.Process(512, wrap, pot, noise);
            ASSERT_TRUE(out >= 0 && out <= kTri);
            if (out != prev)
            {
                distinct_outputs++;
            }
            prev = out;
        }
    }
    // Wander is a moving signal, not a held constant.
    ASSERT_TRUE(distinct_outputs > 10);
}
