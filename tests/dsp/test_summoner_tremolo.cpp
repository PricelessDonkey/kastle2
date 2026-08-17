#include "../harness.hpp"
#include "apps/Summoner/SummonerTremolo.hpp"

using namespace kastle2;

namespace
{
constexpr int32_t kRate = 44000;

q15_t Knob(const float frac)
{
    return static_cast<q15_t>(frac * Q15_MAX + 0.5f);
}

SummonerTremolo Make(const int32_t step_frames, const size_t ratio_index, const q15_t depth)
{
    SummonerTremolo t;
    t.Init(kRate);
    t.SetDepth(depth);
    t.SetPeriodFrames(step_frames, ratio_index);
    return t;
}
}

TEST(SummonerTremolo_FoldBottomIsOffAndUpperHalfNeverIs)
{
    // Index 0 (OFF) exists only at the very bottom of the lower half.
    const auto bottom = SummonerTremolo::FromQ15(0);
    ASSERT_EQ(bottom.ratio_index, static_cast<size_t>(0));
    ASSERT_TRUE(!bottom.bass_exempt);

    // The whole upper half is bass-exempt and never OFF — a bass-exempt "off"
    // would be indistinguishable from off.
    for (float f = 0.51f; f <= 1.0f + 1e-4f; f += 0.01f)
    {
        const auto s = SummonerTremolo::FromQ15(Knob(f));
        ASSERT_TRUE(s.bass_exempt);
        ASSERT_TRUE(s.ratio_index >= 1);
        ASSERT_TRUE(s.ratio_index < SummonerTremolo::kNumRatios);
    }
}

TEST(SummonerTremolo_FoldEdgeAtExactlyFiftyPercent)
{
    // Q15_HALF is the last lower-half position (all voices gated, fastest
    // ratio); one step above it crosses into the bass-exempt half at ratio 1.
    const auto at = SummonerTremolo::FromQ15(Q15_HALF);
    ASSERT_EQ(at.ratio_index, SummonerTremolo::kNumRatios - 1);
    ASSERT_TRUE(!at.bass_exempt);

    const auto above = SummonerTremolo::FromQ15(Q15_HALF + 1);
    ASSERT_EQ(above.ratio_index, static_cast<size_t>(1));
    ASSERT_TRUE(above.bass_exempt);

    // Top of the knob is the fastest ratio again, bass-exempt.
    const auto top = SummonerTremolo::FromQ15(Q15_MAX);
    ASSERT_EQ(top.ratio_index, SummonerTremolo::kNumRatios - 1);
    ASSERT_TRUE(top.bass_exempt);
}

TEST(SummonerTremolo_EachHalfRampsThroughTheRatiosMonotonically)
{
    size_t prev = 0;
    for (float f = 0.0f; f <= 0.5f; f += 0.005f)
    {
        const size_t idx = SummonerTremolo::FromQ15(Knob(f)).ratio_index;
        ASSERT_TRUE(idx >= prev);
        prev = idx;
    }
    ASSERT_EQ(prev, SummonerTremolo::kNumRatios - 1);

    prev = 1;
    for (float f = 0.5001f; f <= 1.0f + 1e-4f; f += 0.005f)
    {
        const size_t idx = SummonerTremolo::FromQ15(Knob(f)).ratio_index;
        ASSERT_TRUE(idx >= prev);
        prev = idx;
    }
    ASSERT_EQ(prev, SummonerTremolo::kNumRatios - 1);
}

TEST(SummonerTremolo_GatePeriodMatchesTheRatioTable)
{
    // 1200 frames per clock step. Dotted is 1.5x the plain value, triplet 2/3x —
    // the whole point of the table (they cut across the pattern).
    constexpr int32_t kStep = 1200;
    SummonerTremolo t;
    t.Init(kRate);

    t.SetPeriodFrames(kStep, 1); // 1/2 = 2 steps
    ASSERT_EQ(t.GetPeriodFrames(), 2400);
    t.SetPeriodFrames(kStep, 2); // 1/4 = 1 step
    ASSERT_EQ(t.GetPeriodFrames(), 1200);
    t.SetPeriodFrames(kStep, 3); // 1/4 dotted = 3/2 step
    ASSERT_EQ(t.GetPeriodFrames(), 1800);
    t.SetPeriodFrames(kStep, 4); // 1/4 triplet = 2/3 step
    ASSERT_EQ(t.GetPeriodFrames(), 800);
    t.SetPeriodFrames(kStep, 5); // 1/8 = 1/2 step
    ASSERT_EQ(t.GetPeriodFrames(), 600);
    t.SetPeriodFrames(kStep, 6); // 1/8 dotted = 3/4 step
    ASSERT_EQ(t.GetPeriodFrames(), 900);
    t.SetPeriodFrames(kStep, 7); // 1/8 triplet = 1/3 step
    ASSERT_EQ(t.GetPeriodFrames(), 400);
    t.SetPeriodFrames(kStep, 8); // 1/16 = 1/4 step
    ASSERT_EQ(t.GetPeriodFrames(), 300);
    t.SetPeriodFrames(kStep, 9); // 1/16 triplet = 1/6 step
    ASSERT_EQ(t.GetPeriodFrames(), 200);
}

TEST(SummonerTremolo_ZeroStepPeriodAndOffHoldTheGateOpen)
{
    // No clock measured yet: nothing may go silent before the first tick.
    SummonerTremolo unmeasured = Make(0, 5, Q15_MAX);
    for (int32_t i = 0; i < 4000; i++)
    {
        ASSERT_EQ(unmeasured.Tick(), Q15_MAX);
    }
    ASSERT_EQ(unmeasured.GetPeriodFrames(), 0);

    // Ratio index 0 = OFF, same story with a valid step period.
    SummonerTremolo off = Make(1200, 0, Q15_MAX);
    for (int32_t i = 0; i < 4000; i++)
    {
        ASSERT_EQ(off.Tick(), Q15_MAX);
    }
}

TEST(SummonerTremolo_DepthSetsTheFloor)
{
    // Depth 0: the gate exists but never leaves full gain.
    SummonerTremolo none = Make(1200, 2, 0);
    for (int32_t i = 0; i < 4000; i++)
    {
        ASSERT_EQ(none.Tick(), Q15_MAX);
    }

    // Depth max: the off phase reaches (near) silence, and the on phase still
    // returns to full — over one period we must see both extremes.
    SummonerTremolo full = Make(1200, 2, Q15_MAX);
    q15_t lowest = Q15_MAX;
    q15_t highest = 0;
    for (int32_t i = 0; i < 3600; i++)
    {
        const q15_t g = full.Tick();
        lowest = g < lowest ? g : lowest;
        highest = g > highest ? g : highest;
    }
    ASSERT_TRUE(lowest <= 4);
    ASSERT_EQ(highest, Q15_MAX);
}

TEST(SummonerTremolo_NoSampleJumpExceedsTheSlewIncrement)
{
    // The anti-click guarantee: every edge is slewed, at every depth and rate.
    const q15_t depths[] = {0, Q15_HALF, Q15_MAX};
    const size_t ratios[] = {2, 4, 9};
    for (const q15_t depth : depths)
    {
        for (const size_t ratio : ratios)
        {
            SummonerTremolo t = Make(1200, ratio, depth);
            q15_t prev = t.Tick();
            for (int32_t i = 0; i < 20000; i++)
            {
                const q15_t g = t.Tick();
                const int32_t jump = g > prev ? g - prev : prev - g;
                ASSERT_TRUE(jump <= t.GetSlewInc());
                prev = g;
            }
        }
    }
}

TEST(SummonerTremolo_ClockTickRelocksThePhase)
{
    // Drive deep into the off phase, then a clock tick restarts the period —
    // the gate must head back toward full immediately, not finish the old cycle.
    SummonerTremolo t = Make(1200, 2, Q15_MAX);
    for (int32_t i = 0; i < 900; i++)
    {
        t.Tick();
    }
    const q15_t before = t.Tick();
    ASSERT_TRUE(before < Q15_HALF); // genuinely in the gap

    t.OnClockTick();
    q15_t prev = before;
    for (int32_t i = 0; i < 200; i++)
    {
        const q15_t g = t.Tick();
        ASSERT_TRUE(g >= prev); // rising, i.e. the phase restarted at the on half
        prev = g;
    }
    ASSERT_TRUE(prev > before);
}

TEST(SummonerTremolo_TempoChangeKeepsThePhaseInRange)
{
    // A tempo change mid-cycle (shorter step) must not leave the phase past the
    // new period, which would strand the gate in the off half.
    SummonerTremolo t = Make(1200, 1, Q15_MAX); // period 2400
    for (int32_t i = 0; i < 2000; i++)
    {
        t.Tick();
    }
    t.SetPeriodFrames(300, 8); // period 75
    ASSERT_EQ(t.GetPeriodFrames(), 75);

    // Over a few of the new periods the gate must reach both extremes again.
    q15_t lowest = Q15_MAX;
    q15_t highest = 0;
    for (int32_t i = 0; i < 3000; i++)
    {
        const q15_t g = t.Tick();
        lowest = g < lowest ? g : lowest;
        highest = g > highest ? g : highest;
    }
    ASSERT_TRUE(lowest < highest);
}
