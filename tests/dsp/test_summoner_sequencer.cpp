#include "../harness.hpp"
#include "apps/Summoner/SummonerSequencer.hpp"

using namespace kastle2;

// --- FeedCvToPot: FEED dead-band handling -------------------------------

TEST(SummonerSequencer_CvZeroAtRest)
{
    // Cable at 0V and the unconnected pull-up rest (~1620) both contribute nothing
    ASSERT_EQ(SummonerSequencer::FeedCvToPot(0), 0);
    ASSERT_EQ(SummonerSequencer::FeedCvToPot(1620), 0);
    ASSERT_EQ(SummonerSequencer::FeedCvToPot(SummonerSequencer::kCvActiveThreshold), 0);
}

TEST(SummonerSequencer_CvFullScaleAtMax)
{
    ASSERT_EQ(SummonerSequencer::FeedCvToPot(POT_MAX), POT_MAX);
    // Over-range readings clamp rather than overflow
    ASSERT_EQ(SummonerSequencer::FeedCvToPot(POT_MAX + 500), POT_MAX);
}

TEST(SummonerSequencer_CvMonotonicAboveThreshold)
{
    int32_t prev = -1;
    for (int32_t analog = SummonerSequencer::kCvActiveThreshold; analog <= POT_MAX; analog += 25)
    {
        const int32_t out = SummonerSequencer::FeedCvToPot(analog);
        ASSERT_TRUE(out >= prev);
        ASSERT_TRUE(out >= 0 && out <= POT_MAX);
        prev = out;
    }
}

// --- DensityToHits ------------------------------------------------------------

TEST(SummonerSequencer_DensityZeroIsSilent)
{
    // The knob normal-breaker: density 0 = no hits at any length
    for (size_t length = 2; length <= 16; length++)
    {
        ASSERT_EQ(SummonerSequencer::DensityToHits(0, length), 0u);
    }
}

TEST(SummonerSequencer_DensityFullIsEveryStep)
{
    // Power-on default: full density = a hit on every step
    for (size_t length = 2; length <= 16; length++)
    {
        ASSERT_EQ(SummonerSequencer::DensityToHits(POT_MAX, length), length);
    }
}

TEST(SummonerSequencer_DensityMidpointIsHalf)
{
    ASSERT_EQ(SummonerSequencer::DensityToHits(POT_HALF, 16), 8u);
    ASSERT_EQ(SummonerSequencer::DensityToHits(POT_HALF, 8), 4u);
}

TEST(SummonerSequencer_DensityClampsOutOfRange)
{
    // Pot + CV sums can exceed the pot range; hits must stay in 0..length
    ASSERT_EQ(SummonerSequencer::DensityToHits(POT_MAX * 2, 16), 16u);
    ASSERT_EQ(SummonerSequencer::DensityToHits(-500, 16), 0u);
}

TEST(SummonerSequencer_DensityMonotonicInDensity)
{
    size_t prev = 0;
    for (int32_t density = 0; density <= POT_MAX; density += 64)
    {
        const size_t hits = SummonerSequencer::DensityToHits(density, 16);
        ASSERT_TRUE(hits >= prev);
        prev = hits;
    }
}

// --- LengthFromMapped -----------------------------------------------------------

TEST(SummonerSequencer_LengthMapping)
{
    ASSERT_EQ(SummonerSequencer::LengthFromMapped(0), 2u);
    ASSERT_EQ(SummonerSequencer::LengthFromMapped(7), 9u);
    ASSERT_EQ(SummonerSequencer::LengthFromMapped(14), 16u);
    // Not-yet-read pot (negative) and over-range both give the power-on default
    ASSERT_EQ(SummonerSequencer::LengthFromMapped(-1), 16u);
    ASSERT_EQ(SummonerSequencer::LengthFromMapped(99), 16u);
}
