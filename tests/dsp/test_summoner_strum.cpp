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
    s.Fire(0, SummonerStrum::Direction::LOW_TO_HIGH, 0.0f);
    ASSERT_EQ(s.Tick(), 0b1111u);
    ASSERT_EQ(s.Tick(), 0u); // one-shot, nothing re-fires
}

TEST(SummonerStrum_LowToHigh_FiresInVoiceOrder)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::LOW_TO_HIGH, 0.0f);
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
    s.Fire(100, SummonerStrum::Direction::HIGH_TO_LOW, 0.0f);
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
        s.Fire(100, SummonerStrum::Direction::LOW_TO_HIGH, 1.0f);
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
    s.Fire(5000, SummonerStrum::Direction::LOW_TO_HIGH, 1.0f);
    std::array<int32_t, SummonerStrum::kNumVoices> first{};
    for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
    {
        first[v] = s.GetCountdown(v);
    }

    s.Fire(5000, SummonerStrum::Direction::LOW_TO_HIGH, 1.0f);
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
    s.Fire(250, SummonerStrum::Direction::LOW_TO_HIGH, 0.0f);
    for (size_t v = 0; v < SummonerStrum::kNumVoices; v++)
    {
        ASSERT_EQ(s.GetCountdown(v), static_cast<int32_t>(v) * 250);
    }
}

TEST(SummonerStrum_Reset_CancelsPendingVoices)
{
    SummonerStrum s;
    s.Init(1);
    s.Fire(100, SummonerStrum::Direction::LOW_TO_HIGH, 0.0f);
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
    s.Fire(999999, SummonerStrum::Direction::LOW_TO_HIGH, 0.0f);
    ASSERT_EQ(s.GetCountdown(3), 3 * SummonerStrum::kMaxStrumFrames);
}
