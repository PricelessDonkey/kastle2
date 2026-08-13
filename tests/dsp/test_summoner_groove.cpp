#include "../harness.hpp"
#include "apps/Summoner/SummonerGroove.hpp"

using namespace kastle2;

namespace
{
constexpr int32_t kPeriod = 3000; // frames per clock tick in these tests

q15_t GroovePct(const int pct)
{
    return static_cast<q15_t>((static_cast<int32_t>(Q15_MAX) * pct) / 100);
}

// Runs one measured clock period: OnClockTick then kPeriod frames of Tick().
// Returns the frame index a deferred fire matured on, or -1 if none did.
int32_t RunPeriod(SummonerGroove &g)
{
    g.OnClockTick();
    int32_t matured = -1;
    for (int32_t f = 0; f < kPeriod; f++)
    {
        if (g.Tick() && matured < 0)
        {
            matured = f;
        }
    }
    return matured;
}
}

TEST(SummonerGroove_ZoneAmounts_AreExclusiveAndRamp)
{
    // Humanize: ramps 0..1 across the bottom third, zero elsewhere
    ASSERT_NEAR(SummonerGroove::HumanizeFromQ15(0), 0.0f, 1e-6f);
    ASSERT_NEAR(SummonerGroove::HumanizeFromQ15(GroovePct(33)), 1.0f, 0.02f);
    ASSERT_NEAR(SummonerGroove::HumanizeFromQ15(GroovePct(50)), 0.0f, 1e-6f);
    ASSERT_NEAR(SummonerGroove::HumanizeFromQ15(GroovePct(100)), 0.0f, 1e-6f);
    // Swing: zero outside the middle third, ramps to kMaxSwing at 66%
    ASSERT_NEAR(SummonerGroove::SwingFromQ15(GroovePct(20)), 0.0f, 1e-6f);
    ASSERT_NEAR(SummonerGroove::SwingFromQ15(GroovePct(66)), SummonerGroove::kMaxSwing, 0.02f);
    ASSERT_NEAR(SummonerGroove::SwingFromQ15(GroovePct(90)), 0.0f, 1e-6f);
    // Skip: zero outside the top third, ramps to kMaxSkipChance at 100%
    ASSERT_NEAR(SummonerGroove::SkipChanceFromQ15(GroovePct(50)), 0.0f, 1e-6f);
    ASSERT_NEAR(SummonerGroove::SkipChanceFromQ15(Q15_MAX), SummonerGroove::kMaxSkipChance, 1e-3f);
}

TEST(SummonerGroove_ZeroGroove_FiresEveryHitImmediately)
{
    SummonerGroove g;
    g.Init(1);
    RunPeriod(g); // establish a period
    for (size_t step = 0; step < 32; step++)
    {
        g.OnClockTick();
        ASSERT_EQ(static_cast<int>(g.ProcessHit(step, 0)),
                  static_cast<int>(SummonerGroove::HitAction::FIRE));
        ASSERT_NEAR(SummonerGroove::HumanizeFromQ15(0), 0.0f, 1e-6f);
    }
}

TEST(SummonerGroove_Swing_DelaysOddStepsOnly)
{
    SummonerGroove g;
    g.Init(1);
    RunPeriod(g);
    RunPeriod(g); // period_frames_ now measured as kPeriod

    // Even steps fire on-grid regardless of swing
    ASSERT_EQ(static_cast<int>(g.ProcessHit(0, GroovePct(66))),
              static_cast<int>(SummonerGroove::HitAction::FIRE));
    ASSERT_EQ(static_cast<int>(g.ProcessHit(2, GroovePct(66))),
              static_cast<int>(SummonerGroove::HitAction::FIRE));

    // Odd steps defer, maturing after ~kMaxSwing of the period
    ASSERT_EQ(static_cast<int>(g.ProcessHit(1, GroovePct(66))),
              static_cast<int>(SummonerGroove::HitAction::DEFERRED));
    const int32_t matured = RunPeriod(g);
    ASSERT_TRUE(matured > 0);
    ASSERT_NEAR(static_cast<float>(matured),
                SummonerGroove::SwingFromQ15(GroovePct(66)) * static_cast<float>(kPeriod), 10.0f);
}

TEST(SummonerGroove_Swing_DelayGrowsMonotonicallyWithKnob)
{
    SummonerGroove g;
    g.Init(1);
    RunPeriod(g);
    RunPeriod(g);

    int32_t prev = 0;
    for (int pct = 40; pct <= 66; pct += 5)
    {
        ASSERT_EQ(static_cast<int>(g.ProcessHit(1, GroovePct(pct))),
                  static_cast<int>(SummonerGroove::HitAction::DEFERRED));
        const int32_t matured = RunPeriod(g);
        ASSERT_TRUE(matured > prev);
        prev = matured;
    }
}

TEST(SummonerGroove_Swing_AdaptsToMeasuredPeriod)
{
    SummonerGroove g;
    g.Init(1);
    // Half-speed clock: 2x the frames per tick
    g.OnClockTick();
    for (int32_t f = 0; f < 2 * kPeriod; f++)
    {
        g.Tick();
    }
    g.OnClockTick();

    ASSERT_EQ(static_cast<int>(g.ProcessHit(1, GroovePct(66))),
              static_cast<int>(SummonerGroove::HitAction::DEFERRED));
    int32_t matured = -1;
    for (int32_t f = 0; f < 4 * kPeriod; f++)
    {
        if (g.Tick())
        {
            matured = f;
            break;
        }
    }
    ASSERT_NEAR(static_cast<float>(matured),
                SummonerGroove::SwingFromQ15(GroovePct(66)) * static_cast<float>(2 * kPeriod), 10.0f);
}

TEST(SummonerGroove_Skip_RateTracksKnobAndRerollsPerHit)
{
    SummonerGroove g;
    g.Init(20260812);
    RunPeriod(g);
    constexpr int kHits = 4000;

    // Full knob: ~kMaxSkipChance of hits cancelled
    int skipped = 0;
    for (int i = 0; i < kHits; i++)
    {
        if (g.ProcessHit(static_cast<size_t>(i), Q15_MAX) == SummonerGroove::HitAction::SKIPPED)
        {
            skipped++;
        }
    }
    float rate = static_cast<float>(skipped) / static_cast<float>(kHits);
    ASSERT_TRUE(rate > 0.55f);
    ASSERT_TRUE(rate < 0.65f);

    // Mid-zone (~83%): about half the max chance
    skipped = 0;
    for (int i = 0; i < kHits; i++)
    {
        if (g.ProcessHit(static_cast<size_t>(i), GroovePct(83)) == SummonerGroove::HitAction::SKIPPED)
        {
            skipped++;
        }
    }
    rate = static_cast<float>(skipped) / static_cast<float>(kHits);
    ASSERT_TRUE(rate > 0.24f);
    ASSERT_TRUE(rate < 0.36f);
}

TEST(SummonerGroove_Skip_NeverDefersAndSwingZoneNeverSkips)
{
    SummonerGroove g;
    g.Init(7);
    RunPeriod(g);
    RunPeriod(g);
    // Swing zone: no hit is ever skipped
    for (int i = 0; i < 500; i++)
    {
        const auto action = g.ProcessHit(static_cast<size_t>(i), GroovePct(50));
        ASSERT_TRUE(action != SummonerGroove::HitAction::SKIPPED);
        g.Reset(); // discard any deferral so the next roll is clean
    }
    // Skip zone: hits either fire on-grid or are cancelled, never swung
    for (int i = 0; i < 500; i++)
    {
        const auto action = g.ProcessHit(static_cast<size_t>(i), GroovePct(90));
        ASSERT_TRUE(action != SummonerGroove::HitAction::DEFERRED);
    }
}

TEST(SummonerGroove_Reset_CancelsPendingDeferredFire)
{
    SummonerGroove g;
    g.Init(1);
    RunPeriod(g);
    RunPeriod(g);
    ASSERT_EQ(static_cast<int>(g.ProcessHit(1, GroovePct(66))),
              static_cast<int>(SummonerGroove::HitAction::DEFERRED));
    g.Reset();
    ASSERT_EQ(RunPeriod(g), -1);
}
