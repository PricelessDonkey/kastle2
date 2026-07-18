#include "../harness.hpp"
#include "common/dsp/utility/Slewer.hpp"

using namespace kastle2;

TEST(Slewer_InitStartsAtZero)
{
    Slewer s;
    s.Init();
    ASSERT_EQ(s.GetValue(), 0);
    ASSERT_TRUE(s.IsAtTarget());
}

TEST(Slewer_StepsBySpeedTowardTarget)
{
    Slewer s;
    s.Init();
    s.SetSpeed(10);
    s.SetValue(100);

    ASSERT_EQ(s.Process(), 10);
    ASSERT_EQ(s.Process(), 20);
    ASSERT_FALSE(s.IsAtTarget());

    for (int i = 0; i < 7; ++i)
    {
        s.Process();
    }
    ASSERT_EQ(s.GetValue(), 90);
    ASSERT_EQ(s.Process(), 100); // last step snaps exactly to target, no overshoot
    ASSERT_TRUE(s.IsAtTarget());

    // Further Process() calls hold at target.
    ASSERT_EQ(s.Process(), 100);
}

TEST(Slewer_SlewsDownwardToo)
{
    Slewer s;
    s.Init();
    s.SetSpeed(5);
    s.SetValue(-20);
    ASSERT_EQ(s.Process(), -5);
    ASSERT_EQ(s.Process(), -10);
    ASSERT_EQ(s.Process(), -15);
    ASSERT_EQ(s.Process(), -20);
    ASSERT_TRUE(s.IsAtTarget());
}

TEST(Slewer_ZeroOrNegativeSpeedClampsToOne)
{
    Slewer s;
    s.Init();
    s.SetSpeed(0);
    s.SetValue(3);
    ASSERT_EQ(s.Process(), 1);
    s.SetSpeed(-5);
    s.SetValue(10);
    ASSERT_EQ(s.Process(), 2); // still stepping by 1, not -5
}

TEST(Slewer_JumpSnapsImmediately)
{
    Slewer s;
    s.Init();
    s.SetSpeed(1);
    s.SetValue(500);
    s.Jump();
    ASSERT_EQ(s.GetValue(), 500);
    ASSERT_TRUE(s.IsAtTarget());
}

TEST(Slewer_RetargetingMidSlewChangesDirection)
{
    Slewer s;
    s.Init();
    s.SetSpeed(10);
    s.SetValue(100);
    s.Process(); // at 10
    s.Process(); // at 20
    s.SetValue(0);
    ASSERT_EQ(s.Process(), 10); // now heading back down
    ASSERT_EQ(s.Process(), 0);
    ASSERT_TRUE(s.IsAtTarget());
}
