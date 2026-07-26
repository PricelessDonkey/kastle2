#include "../harness.hpp"
#include "apps/Summoner/SummonerComboLayer.hpp"

using namespace kastle2;

namespace
{
using Raw = std::array<int32_t, SummonerComboLayer::kNumSlots>;

Raw MakeRaw(const int32_t value)
{
    Raw raw;
    raw.fill(value);
    return raw;
}

constexpr int32_t kOver = SummonerComboLayer::kEngageThreshold + 1;
}

TEST(ComboLayer_InactiveDoesNothing)
{
    SummonerComboLayer combo;
    combo.Init();
    combo.SetSlotValue(0, 1234);

    // Pots move freely while the combo isn't held — no pickup, no change
    combo.Process(false, MakeRaw(0));
    combo.Process(false, MakeRaw(4000));

    ASSERT_FALSE(combo.IsActive());
    ASSERT_FALSE(combo.AnyEngaged());
    ASSERT_EQ(combo.GetValue(0), 1234);
}

TEST(ComboLayer_HoldWithoutMovementEngagesNothing)
{
    SummonerComboLayer combo;
    combo.Init();
    combo.SetSlotValue(2, 777);

    const Raw parked = MakeRaw(2000);
    combo.Process(true, parked);
    for (int i = 0; i < 50; i++)
    {
        combo.Process(true, parked);
    }

    ASSERT_TRUE(combo.IsActive());
    ASSERT_FALSE(combo.AnyEngaged());
    ASSERT_EQ(combo.GetValue(2), 777);

    combo.Process(false, parked);
    ASSERT_TRUE(combo.JustEnded());
    ASSERT_FALSE(combo.AnyEngaged());
}

TEST(ComboLayer_SmallWiggleStaysBelowThreshold)
{
    SummonerComboLayer combo;
    combo.Init();
    combo.SetSlotValue(1, 500);

    Raw raw = MakeRaw(2000);
    combo.Process(true, raw);
    raw[1] = 2000 + SummonerComboLayer::kEngageThreshold; // exactly at threshold: not over
    combo.Process(true, raw);

    ASSERT_FALSE(combo.IsEngaged(1));
    ASSERT_EQ(combo.GetValue(1), 500);
}

TEST(ComboLayer_EngageAndTrack)
{
    SummonerComboLayer combo;
    combo.Init();
    combo.SetSlotValue(3, 100);

    Raw raw = MakeRaw(2000);
    combo.Process(true, raw);

    raw[3] = 2000 + kOver;
    combo.Process(true, raw);
    ASSERT_TRUE(combo.IsEngaged(3));
    ASSERT_TRUE(combo.AnyEngaged());
    ASSERT_TRUE(combo.HasChanged(3));
    ASSERT_EQ(combo.GetValue(3), 2000 + kOver);

    // Once engaged, tracks every move — including back toward the reference
    raw[3] = 1500;
    combo.Process(true, raw);
    ASSERT_EQ(combo.GetValue(3), 1500);

    // Static pot: engaged but no change reported
    combo.Process(true, raw);
    ASSERT_FALSE(combo.HasChanged(3));
}

TEST(ComboLayer_EngageWorksInBothDirections)
{
    SummonerComboLayer combo;
    combo.Init();

    Raw raw = MakeRaw(2000);
    combo.Process(true, raw);
    raw[5] = 2000 - kOver;
    combo.Process(true, raw);

    ASSERT_TRUE(combo.IsEngaged(5));
    ASSERT_EQ(combo.GetValue(5), 2000 - kOver);
}

TEST(ComboLayer_SlotsAreIndependent)
{
    SummonerComboLayer combo;
    combo.Init();
    combo.SetSlotValue(0, 10);
    combo.SetSlotValue(6, 20);

    Raw raw = MakeRaw(2000);
    combo.Process(true, raw);
    raw[0] = 2000 + kOver;
    combo.Process(true, raw);

    ASSERT_TRUE(combo.IsEngaged(0));
    ASSERT_FALSE(combo.IsEngaged(6));
    ASSERT_EQ(combo.GetValue(6), 20); // untouched slot keeps its value
}

TEST(ComboLayer_ValuesPersistAcrossHolds)
{
    SummonerComboLayer combo;
    combo.Init();

    Raw raw = MakeRaw(2000);
    combo.Process(true, raw);
    raw[2] = 3000;
    combo.Process(true, raw);
    ASSERT_EQ(combo.GetValue(2), 3000);

    combo.Process(false, raw);
    ASSERT_TRUE(combo.JustEnded());
    ASSERT_EQ(combo.GetValue(2), 3000);

    // JustEnded is a one-shot flag
    combo.Process(false, raw);
    ASSERT_FALSE(combo.JustEnded());
    ASSERT_EQ(combo.GetValue(2), 3000);
}

TEST(ComboLayer_NewHoldRequiresNewMovement)
{
    SummonerComboLayer combo;
    combo.Init();

    // First hold: engage slot 4 and park it at 3500
    Raw raw = MakeRaw(2000);
    combo.Process(true, raw);
    raw[4] = 3500;
    combo.Process(true, raw);
    combo.Process(false, raw);
    ASSERT_EQ(combo.GetValue(4), 3500);

    // Second hold with the pot parked at 3500: reference re-snapshots there,
    // so nothing engages until it moves again
    combo.Process(true, raw);
    ASSERT_FALSE(combo.IsEngaged(4));
    ASSERT_FALSE(combo.AnyEngaged());
    combo.Process(true, raw);
    ASSERT_FALSE(combo.AnyEngaged());

    raw[4] = 3500 - kOver;
    combo.Process(true, raw);
    ASSERT_TRUE(combo.IsEngaged(4));
    ASSERT_EQ(combo.GetValue(4), 3500 - kOver);
}

TEST(ComboLayer_MovementBeforeHoldDoesNotEngage)
{
    SummonerComboLayer combo;
    combo.Init();
    combo.SetSlotValue(1, 42);

    // Pot travels while combo is up, then the hold starts: reference is taken
    // at hold start, so the earlier travel must not count as movement
    combo.Process(false, MakeRaw(500));
    combo.Process(true, MakeRaw(3000));
    combo.Process(true, MakeRaw(3000));

    ASSERT_FALSE(combo.AnyEngaged());
    ASSERT_EQ(combo.GetValue(1), 42);
}
