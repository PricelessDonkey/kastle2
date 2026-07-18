#include "../harness.hpp"
#include "common/dsp/utility/EuclideanPattern.hpp"

using namespace kastle2;

namespace
{
// Steps the generator through one full cycle and packs the hits into a
// bitmask (bit i = step i) for comparison against a known pattern.
uint16_t CycleBits(EuclideanPattern &e)
{
    uint16_t bits = 0;
    for (size_t i = 0; i < e.GetLength(); i++)
    {
        if (e.Step())
        {
            bits |= static_cast<uint16_t>(1u << i);
        }
    }
    return bits;
}
}

TEST(EuclideanPattern_ThreeInEight_IsTresillo)
{
    // E(3,8) = 10010010 (gaps 3,3,2) — the Cuban tresillo, the canonical
    // Bjorklund result in Toussaint's paper.
    EuclideanPattern e;
    e.Init();
    e.SetPattern(3, 8);
    ASSERT_EQ(CycleBits(e), 0b01001001u); // bit 0 = step 0
}

TEST(EuclideanPattern_FiveInSixteen_MatchesToussaint)
{
    // E(5,16) = 1001001001001000 (gaps 3,3,3,3,4), per Toussaint.
    EuclideanPattern e;
    e.Init();
    e.SetPattern(5, 16);
    ASSERT_EQ(CycleBits(e), 0b0001001001001001u);
}

TEST(EuclideanPattern_ZeroHits_IsSilent)
{
    EuclideanPattern e;
    e.Init();
    e.SetPattern(0, 8);
    for (size_t i = 0; i < 16; i++) // two full cycles
    {
        ASSERT_FALSE(e.Step());
    }
}

TEST(EuclideanPattern_FullDensity_HitsEveryStep)
{
    EuclideanPattern e;
    e.Init();
    e.SetPattern(16, 16);
    for (size_t i = 0; i < 32; i++)
    {
        ASSERT_TRUE(e.Step());
    }
}

TEST(EuclideanPattern_InitDefault_IsFullDensity)
{
    // Power-on default per CHORD-GEN.md: density = K, chord on every tick.
    EuclideanPattern e;
    e.Init();
    ASSERT_EQ(e.GetHits(), EuclideanPattern::kMaxSteps);
    ASSERT_TRUE(e.Step());
}

TEST(EuclideanPattern_FirstStepIsAlwaysAHit)
{
    // Canonical rotation: any non-empty pattern leads with a hit.
    for (size_t length = 1; length <= EuclideanPattern::kMaxSteps; length++)
    {
        for (size_t hits = 1; hits <= length; hits++)
        {
            EuclideanPattern e;
            e.Init();
            e.SetPattern(hits, length);
            ASSERT_TRUE(e.GetStepValue(0));
        }
    }
}

TEST(EuclideanPattern_HitCountMatchesDensityExactly)
{
    for (size_t length = 1; length <= EuclideanPattern::kMaxSteps; length++)
    {
        for (size_t hits = 0; hits <= length; hits++)
        {
            EuclideanPattern e;
            e.Init();
            e.SetPattern(hits, length);
            size_t counted = 0;
            for (size_t i = 0; i < length; i++)
            {
                if (e.Step())
                {
                    counted++;
                }
            }
            ASSERT_EQ(counted, hits);
        }
    }
}

TEST(EuclideanPattern_RecomputeSameParams_IsIdentical)
{
    EuclideanPattern a;
    a.Init();
    a.SetPattern(7, 16);

    EuclideanPattern b;
    b.Init();
    b.SetPattern(5, 12); // different intermediate state
    b.SetPattern(7, 16);

    ASSERT_EQ(CycleBits(a), CycleBits(b));
}

TEST(EuclideanPattern_DensityChangeMidCycle_PreservesStepPosition)
{
    // Live density tweaks must not re-phase the groove.
    EuclideanPattern e;
    e.Init();
    e.SetPattern(3, 8);
    e.Step();
    e.Step();
    e.Step();
    ASSERT_EQ(e.GetStep(), size_t{3});

    e.SetPattern(5, 8);
    ASSERT_EQ(e.GetStep(), size_t{3});
}

TEST(EuclideanPattern_LengthShrinkBelowStep_WrapsStepIntoRange)
{
    EuclideanPattern e;
    e.Init();
    e.SetPattern(3, 16);
    for (size_t i = 0; i < 10; i++)
    {
        e.Step();
    }
    ASSERT_EQ(e.GetStep(), size_t{10});

    e.SetPattern(3, 8);
    ASSERT_EQ(e.GetStep(), size_t{2}); // 10 mod 8
}

TEST(EuclideanPattern_Reset_SnapsToStepZero)
{
    EuclideanPattern e;
    e.Init();
    e.SetPattern(3, 8);
    e.Step();
    e.Step();
    e.Reset();
    ASSERT_EQ(e.GetStep(), size_t{0});
    ASSERT_TRUE(e.Step()); // step 0 of the tresillo is a hit
}

TEST(EuclideanPattern_ClampsHitsAndLength)
{
    EuclideanPattern e;
    e.Init();
    e.SetPattern(99, 99);
    ASSERT_EQ(e.GetLength(), EuclideanPattern::kMaxSteps);
    ASSERT_EQ(e.GetHits(), EuclideanPattern::kMaxSteps);

    e.SetPattern(5, 0);
    ASSERT_EQ(e.GetLength(), size_t{1});
    ASSERT_EQ(e.GetHits(), size_t{1});
}
