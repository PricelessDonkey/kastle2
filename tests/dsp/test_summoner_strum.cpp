#include "../harness.hpp"
#include "apps/Summoner/SummonerStrum.hpp"

using namespace kastle2;

namespace
{
// Ticks until every pending voice has fired; records each voice's fire frame.
// Returns false if anything is still pending after max_frames.
bool CollectFireFrames(SummonerStrum &s, std::array<int32_t, SummonerStrum::kNumVoices> &frames,
                       const int32_t max_frames = 100000)
{
    frames.fill(-1);
    for (int32_t frame = 0; frame < max_frames; frame++)
    {
        const uint32_t fired = s.Tick();
        for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
        {
            if (fired & (1u << v))
            {
                frames[v] = frame;
            }
        }
        bool all_done = true;
        for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
        {
            if (s.GetCountdown(v) >= 0)
            {
                all_done = false;
            }
        }
        if (all_done)
        {
            return true;
        }
    }
    return false;
}
}

TEST(SummonerStrum_ZeroStrum_AllVoicesFireOnFrameZero)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(0, SummonerStrum::Direction::UP, 0.0f);
    ASSERT_EQ(s.Tick(), 0b1111u);
    ASSERT_EQ(s.Tick(), 0u); // one-shot, nothing re-fires
}

TEST(SummonerStrum_LowToHigh_FiresInVoiceOrder)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::UP, 0.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> frames;
    ASSERT_TRUE(CollectFireFrames(s, frames));
    ASSERT_EQ(frames[0], 0);
    ASSERT_EQ(frames[1], 100);
    ASSERT_EQ(frames[2], 200);
    ASSERT_EQ(frames[3], 300);
}

TEST(SummonerStrum_HighToLow_FiresTopVoiceFirst)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::DOWN, 0.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> frames;
    ASSERT_TRUE(CollectFireFrames(s, frames));
    ASSERT_EQ(frames[3], 0);
    ASSERT_EQ(frames[2], 100);
    ASSERT_EQ(frames[1], 200);
    ASSERT_EQ(frames[0], 300);
}

TEST(SummonerStrum_OutToIn_FiresOutermostFirst)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::OUT_TO_IN, 0.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> frames;
    ASSERT_TRUE(CollectFireFrames(s, frames));
    // Fire order 0, 3, 1, 2
    ASSERT_EQ(frames[0], 0);
    ASSERT_EQ(frames[3], 100);
    ASSERT_EQ(frames[1], 200);
    ASSERT_EQ(frames[2], 300);
}

TEST(SummonerStrum_HumanizeJitter_IsBounded)
{
    SummonerStrum s;
    s.Init(12345);
    for (int fire = 0; fire < 50; fire++)
    {
        s.Fire(100, SummonerStrum::Direction::UP, 1.0f);
        for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
        {
            const int32_t nominal = static_cast<int32_t>(v) * 100;
            const int32_t delta = s.GetCountdown(v) - nominal;
            ASSERT_TRUE(delta <= SummonerStrum::kMaxHumanizeFrames);
            // Early jitter clamps at frame 0, never goes negative
            ASSERT_TRUE(s.GetCountdown(v) >= 0);
            ASSERT_TRUE(delta >= -SummonerStrum::kMaxHumanizeFrames);
        }
        s.Reset();
    }
}

TEST(SummonerStrum_HumanizeJitter_RerollsEachFire)
{
    SummonerStrum s;
    s.Init(777);
    // Large strum so jitter isn't clamped at zero for the later voices.
    s.Fire(5000, SummonerStrum::Direction::UP, 1.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> first{};
    for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
    {
        first[v] = s.GetCountdown(v);
    }

    s.Fire(5000, SummonerStrum::Direction::UP, 1.0f);
    bool any_different = false;
    for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
    {
        if (s.GetCountdown(v) != first[v])
        {
            any_different = true;
        }
    }
    ASSERT_TRUE(any_different);
}

TEST(SummonerStrum_ZeroHumanize_IsMachineTight)
{
    SummonerStrum s;
    s.Init(999);
    s.Fire(250, SummonerStrum::Direction::UP, 0.0f);
    for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
    {
        ASSERT_EQ(s.GetCountdown(v), static_cast<int32_t>(v) * 250);
    }
}

TEST(SummonerStrum_Reset_CancelsPendingVoices)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::UP, 0.0f);
    s.Tick(); // voice 0 fires
    s.Reset();
    for (int32_t frame = 0; frame < 500; frame++)
    {
        ASSERT_EQ(s.Tick(), 0u);
    }
}

TEST(SummonerStrum_StrumFramesClampedToMax)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(999999, SummonerStrum::Direction::UP, 0.0f);
    ASSERT_EQ(s.GetCountdown(3), 3 * SummonerStrum::kMaxStrumFrames);
}

TEST(SummonerStrum_InToOut_FiresInnermostFirst)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::IN_TO_OUT, 0.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> frames;
    ASSERT_TRUE(CollectFireFrames(s, frames));
    // Fire order 1, 2, 0, 3
    ASSERT_EQ(frames[1], 0);
    ASSERT_EQ(frames[2], 100);
    ASSERT_EQ(frames[0], 200);
    ASSERT_EQ(frames[3], 300);
}

TEST(SummonerStrum_UpDown_AlternatesPerFire)
{
    SummonerStrum s;
    s.Init(1);
    // First fire = Up order
    s.Fire(100, SummonerStrum::Direction::UP_DOWN, 0.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> frames;
    ASSERT_TRUE(CollectFireFrames(s, frames));
    ASSERT_EQ(frames[0], 0);
    ASSERT_EQ(frames[3], 300);
    // Second fire = Down order
    s.Fire(100, SummonerStrum::Direction::UP_DOWN, 0.0f);
    ASSERT_TRUE(CollectFireFrames(s, frames));
    ASSERT_EQ(frames[3], 0);
    ASSERT_EQ(frames[0], 300);
    // Third fire = Up again
    s.Fire(100, SummonerStrum::Direction::UP_DOWN, 0.0f);
    ASSERT_TRUE(CollectFireFrames(s, frames));
    ASSERT_EQ(frames[0], 0);
    // Reset restores the alternation to Up
    s.Fire(100, SummonerStrum::Direction::UP_DOWN, 0.0f); // would be Down...
    s.Reset();
    s.Fire(100, SummonerStrum::Direction::UP_DOWN, 0.0f); // ...but reset -> Up
    ASSERT_TRUE(CollectFireFrames(s, frames));
    ASSERT_EQ(frames[0], 0);
    ASSERT_EQ(frames[3], 300);
}

TEST(SummonerStrum_Random_IsAlwaysAValidPermutation)
{
    SummonerStrum s;
    s.Init(4242);
    for (int fire = 0; fire < 200; fire++)
    {
        s.Fire(100, SummonerStrum::Direction::RANDOM, 0.0f);
        std::array<int32_t, SummonerStrum::kNumVoices> frames;
        ASSERT_TRUE(CollectFireFrames(s, frames));
        // Every voice fires exactly once, on a distinct multiple of 100
        std::array<bool, SummonerStrum::kNumVoices> slot_used{};
        for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
        {
            ASSERT_TRUE(frames[v] >= 0);
            ASSERT_EQ(frames[v] % 100, 0);
            const size_t slot = static_cast<size_t>(frames[v] / 100);
            ASSERT_TRUE(slot < SummonerStrum::kNumVoices);
            ASSERT_TRUE(!slot_used[slot]);
            slot_used[slot] = true;
        }
    }
}

TEST(SummonerStrum_Random_RerollsEveryFireAndCoversOrders)
{
    SummonerStrum s;
    s.Init(31337);
    // Encode each fire's permutation as a base-4 signature; over many fires a
    // fresh per-trigger shuffle must produce several distinct orders (a shuffle
    // fixed per pattern would produce exactly one)
    std::array<int, 256> seen{};
    int distinct = 0;
    for (int fire = 0; fire < 300; fire++)
    {
        s.Fire(100, SummonerStrum::Direction::RANDOM, 0.0f);
        std::array<int32_t, SummonerStrum::kNumVoices> frames;
        ASSERT_TRUE(CollectFireFrames(s, frames));
        int sig = 0;
        for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
        {
            sig = sig * 4 + static_cast<int>(frames[v] / 100);
        }
        if (seen[sig] == 0)
        {
            distinct++;
        }
        seen[sig]++;
    }
    // 4! = 24 possible orders; 300 uniform draws should see most of them
    ASSERT_TRUE(distinct >= 12);
}

TEST(SummonerStrum_DirectionFromQ15_ZoneBoundaries)
{
    using D = SummonerStrum::Direction;
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(0)), static_cast<int>(D::UP));
    // Six equal zones over 0..Q15_MAX; probe each zone's center
    const int32_t sixth = (Q15_MAX + 1) / 6;
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(static_cast<q15_t>(sixth / 2))), static_cast<int>(D::UP));
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(static_cast<q15_t>(sixth + sixth / 2))), static_cast<int>(D::DOWN));
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(static_cast<q15_t>(2 * sixth + sixth / 2))), static_cast<int>(D::OUT_TO_IN));
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(static_cast<q15_t>(3 * sixth + sixth / 2))), static_cast<int>(D::IN_TO_OUT));
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(static_cast<q15_t>(4 * sixth + sixth / 2))), static_cast<int>(D::UP_DOWN));
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(static_cast<q15_t>(5 * sixth + sixth / 2))), static_cast<int>(D::RANDOM));
    ASSERT_EQ(static_cast<int>(SummonerStrum::DirectionFromQ15(Q15_MAX)), static_cast<int>(D::RANDOM));
}

TEST(SummonerStrum_SkipChance_ZeroAcrossTheMiddle)
{
    // 4%..96% of the range must have exactly zero skip chance
    for (int pct = 4; pct <= 96; pct++)
    {
        const q15_t v = static_cast<q15_t>((static_cast<int32_t>(Q15_MAX) * pct) / 100);
        ASSERT_NEAR(SummonerStrum::SkipChanceFromQ15(v), 0.0f, 1e-6f);
    }
}

TEST(SummonerStrum_SkipChance_RampsMonotonicallyAtBothEnds)
{
    // Bottom edge: max at 0, monotonic down to 0 at 3%
    ASSERT_NEAR(SummonerStrum::SkipChanceFromQ15(0), SummonerStrum::kMaxSkipChance, 1e-3f);
    float prev = SummonerStrum::SkipChanceFromQ15(0);
    for (int i = 1; i <= 30; i++)
    {
        // 0..3% in 0.1% steps
        const q15_t v = static_cast<q15_t>((static_cast<int32_t>(Q15_MAX) * i) / 1000);
        const float c = SummonerStrum::SkipChanceFromQ15(v);
        ASSERT_TRUE(c <= prev + 1e-6f);
        prev = c;
    }
    // Top edge: 0 at 97%, monotonic up to max at 100%
    prev = 0.0f;
    for (int i = 0; i <= 30; i++)
    {
        const q15_t v = static_cast<q15_t>(Q15_MAX - (static_cast<int32_t>(Q15_MAX) * (30 - i)) / 1000);
        const float c = SummonerStrum::SkipChanceFromQ15(v);
        ASSERT_TRUE(c >= prev - 1e-6f);
        prev = c;
    }
    ASSERT_NEAR(SummonerStrum::SkipChanceFromQ15(Q15_MAX), SummonerStrum::kMaxSkipChance, 1e-3f);
}

TEST(SummonerStrum_Skip_NeverDropsRootAndRateTracksChance)
{
    SummonerStrum s;
    s.Init(2026);
    int skipped = 0;
    constexpr int kFires = 2000;
    for (int fire = 0; fire < kFires; fire++)
    {
        s.Fire(0, SummonerStrum::Direction::UP, 0.0f, 0.4f);
        const uint32_t fired = s.Tick();
        ASSERT_TRUE((fired & 1u) != 0u); // root always fires
        for (size_t v = 1; v < SummonerStrum::kNumVoices; v++)
        {
            if ((fired & (1u << v)) == 0u)
            {
                skipped++;
            }
        }
        s.Reset();
    }
    // 3 skippable voices x 2000 fires at p=0.4 -> expect ~2400 skips
    const float rate = static_cast<float>(skipped) / static_cast<float>(kFires * 3);
    ASSERT_TRUE(rate > 0.35f);
    ASSERT_TRUE(rate < 0.45f);
}

TEST(SummonerStrum_Skip_RerollsEveryFire)
{
    SummonerStrum s;
    s.Init(555);
    // At p=0.5 the skipped-voice mask must vary across fires
    uint32_t first_mask = 0;
    bool any_different = false;
    for (int fire = 0; fire < 50; fire++)
    {
        s.Fire(0, SummonerStrum::Direction::UP, 0.0f, 0.5f);
        const uint32_t fired = s.Tick();
        if (fire == 0)
        {
            first_mask = fired;
        }
        else if (fired != first_mask)
        {
            any_different = true;
        }
        s.Reset();
    }
    ASSERT_TRUE(any_different);
}

TEST(SummonerStrum_ZeroSkipChance_FullChordAlways)
{
    SummonerStrum s;
    s.Init(9);
    for (int fire = 0; fire < 100; fire++)
    {
        s.Fire(0, SummonerStrum::Direction::RANDOM, 0.0f, 0.0f);
        ASSERT_EQ(s.Tick(), 0b1111u);
        s.Reset();
    }
}
