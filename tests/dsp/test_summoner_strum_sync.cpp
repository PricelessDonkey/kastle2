#include "../harness.hpp"
#include "apps/Summoner/SummonerStrum.hpp"
#include "apps/Summoner/SummonerStrumSync.hpp"

using namespace kastle2;

namespace
{
q15_t Knob(const float frac)
{
    return static_cast<q15_t>(frac * Q15_MAX + 0.5f);
}

// One clock step at 4 Hz / 44kHz — divides evenly by every denominator in the
// table, so the expected frame counts below are exact.
constexpr int32_t kStep = 11000 * 4 * 3; // 132000 frames
} // namespace

TEST(SummonerStrumSync_KnobBottomIsABlockChord)
{
    ASSERT_EQ(SummonerStrumSync::IndexFromQ15(0), 0u);
    ASSERT_EQ(SummonerStrumSync::FramesFromQ15(0, kStep), 0);
}

TEST(SummonerStrumSync_KnobTopIsHalfAStep)
{
    ASSERT_EQ(SummonerStrumSync::IndexFromQ15(Q15_MAX), SummonerStrumSync::kNumRatios - 1);
    ASSERT_EQ(SummonerStrumSync::FramesFromQ15(Q15_MAX, kStep), kStep / 2);
}

TEST(SummonerStrumSync_IndexIsMonotonicAcrossTheKnob)
{
    // Ten equal zones, no fold: the index never goes backwards and every entry
    // is reachable.
    size_t prev = 0;
    size_t distinct = 1;
    for (int i = 1; i <= 1000; ++i)
    {
        const size_t idx = SummonerStrumSync::IndexFromQ15(Knob(static_cast<float>(i) / 1000.0f));
        ASSERT_TRUE(idx >= prev);
        if (idx != prev)
        {
            distinct++;
        }
        prev = idx;
    }
    ASSERT_EQ(distinct, SummonerStrumSync::kNumRatios);
}

TEST(SummonerStrumSync_SpacingIsStrictlyAscendingAboveTheBlockChord)
{
    // Every ratio is longer than the one below it — a knob turn always slows the
    // cascade, never speeds it up.
    int32_t prev = 0;
    for (size_t i = 1; i < SummonerStrumSync::kNumRatios; ++i)
    {
        const auto &r = SummonerStrumSync::kRatios[i];
        const int32_t frames = static_cast<int32_t>(static_cast<int64_t>(kStep) * r.num / r.den);
        ASSERT_TRUE(frames > prev);
        prev = frames;
    }
}

TEST(SummonerStrumSync_DottedAndTripletSitBetweenTheStraightSubdivisions)
{
    // 1/8 dotted is 1.5x a straight 1/8; 1/8 triplet is 2/3 of one. This is the
    // whole point of the table — a strum that pushes against the sequencer.
    const int32_t eighth = static_cast<int32_t>(static_cast<int64_t>(kStep) * 1 / 8);
    ASSERT_EQ(static_cast<int64_t>(kStep) * SummonerStrumSync::kRatios[7].num /
                  SummonerStrumSync::kRatios[7].den,
              eighth * 3 / 2);
    ASSERT_EQ(static_cast<int64_t>(kStep) * SummonerStrumSync::kRatios[4].num /
                  SummonerStrumSync::kRatios[4].den,
              eighth * 2 / 3);
}

TEST(SummonerStrumSync_SpacingScalesWithTempo)
{
    // The same knob position at half the tempo gives twice the spacing — that is
    // what "synced" means here.
    const q15_t knob = Knob(0.85f);
    ASSERT_EQ(SummonerStrumSync::FramesFromQ15(knob, kStep * 2),
              SummonerStrumSync::FramesFromQ15(knob, kStep) * 2);
}

TEST(SummonerStrumSync_UnmeasuredClockUsesTheFallbackStep)
{
    // Before two clock ticks have been seen GetPeriodFrames() returns 0; the
    // first chords still strum instead of collapsing to block chords.
    const q15_t knob = Knob(0.95f);
    ASSERT_EQ(SummonerStrumSync::FramesFromQ15(knob, 0),
              SummonerStrumSync::FramesFromQ15(knob, SummonerStrumSync::kFallbackStepFrames));
    ASSERT_TRUE(SummonerStrumSync::FramesFromQ15(knob, 0) > 0);
}

TEST(SummonerStrumSync_OutOfRangeKnobClampsToTheEnds)
{
    ASSERT_EQ(SummonerStrumSync::IndexFromQ15(static_cast<q15_t>(-5000)), 0u);
    ASSERT_EQ(SummonerStrumSync::IndexFromQ15(Q15_MAX), SummonerStrumSync::kNumRatios - 1);
}

TEST(SummonerStrumSync_SlowestTempoTopEntryFitsUnderTheStrumClamp)
{
    // Slowest internal tempo is 0.5 Hz (a 2s step); half of that is 1s, exactly
    // SummonerStrum's raised clamp — so the top entry is honoured, not truncated.
    constexpr int32_t kSlowestStep = 88200; // 2s at 44.1kHz
    ASSERT_TRUE(SummonerStrumSync::FramesFromQ15(Q15_MAX, kSlowestStep) <=
                SummonerStrum::kMaxStrumFrames);
}
