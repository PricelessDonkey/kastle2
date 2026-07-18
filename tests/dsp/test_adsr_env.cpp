#include "../harness.hpp"
#include "common/dsp/control/AdsrEnv.hpp"

using namespace kastle2;

namespace
{
constexpr float kSampleRate = 1000.0f;

// Runs Process() until the predicate is true or the iteration cap is hit.
// Returns the number of Process() calls made. Fails the test if the cap
// is reached without the predicate becoming true.
template <typename Pred>
int run_until(AdsrEnv &env, Pred pred, int max_iterations = 1000)
{
    for (int i = 0; i < max_iterations; ++i)
    {
        env.Process();
        if (pred())
        {
            return i + 1;
        }
    }
    ASSERT_TRUE(false); // exceeded max_iterations without reaching the predicate
    return -1;
}
} // namespace

TEST(AdsrEnv_StartsIdleWithZeroOutput)
{
    AdsrEnv env;
    env.Init(kSampleRate);
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::IDLE);
    ASSERT_EQ(env.GetOutput(), 0);
    ASSERT_FALSE(env.IsActive());
}

TEST(AdsrEnv_TriggerStartsAttackOnNextProcessCall)
{
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.05f); // 50 samples at 1kHz — won't hit max in one step
    env.Trigger();
    // Trigger() only sets a pending flag; state flips on the next Process().
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::IDLE);
    env.Process();
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::ATTACK);
    ASSERT_TRUE(env.IsActive());
}

TEST(AdsrEnv_FullCycleAttackDecaySustainAtExactLevel)
{
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.05f);
    env.SetDecayTime(0.05f);
    const q31_t sustain = q31(0.4f);
    env.SetSustainLevel(sustain);
    env.Trigger();

    run_until(env, [&] { return env.GetOutput() == Q31_MAX; });
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::DECAY);

    run_until(env, [&] { return env.GetState() == AdsrEnv::State::SUSTAIN; });
    // The DECAY branch snaps output to exactly sustain_level_ on crossing it.
    ASSERT_EQ(env.GetOutput(), sustain);

    // SUSTAIN just holds — no further movement.
    for (int i = 0; i < 20; ++i)
    {
        env.Process();
    }
    ASSERT_EQ(env.GetOutput(), sustain);
}

TEST(AdsrEnv_IsActiveIsFalseDuringSustain)
{
    // Surprising but intentional per the source: IsActive() only covers
    // ATTACK/DECAY/HOLD (plus a pending trigger) — SUSTAIN reads as inactive
    // even though the envelope is holding a non-zero output.
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.02f);
    env.SetDecayTime(0.02f);
    env.SetSustainLevel(q31(0.5f));
    env.Trigger();
    run_until(env, [&] { return env.GetState() == AdsrEnv::State::SUSTAIN; });
    ASSERT_FALSE(env.IsActive());
    ASSERT_TRUE(env.GetOutput() > 0);
}

TEST(AdsrEnv_RetriggerRestartsAttackFromZero)
{
    // Process() applies a pending Trigger() by zeroing output_ and setting
    // state_ = ATTACK, then falls straight through into the ATTACK branch
    // of the same switch statement — so GetOutput() right after the
    // retriggering Process() call is NOT 0, it's already one attack step
    // past zero. Compare against a freshly-triggered envelope with
    // identical settings to pin down that exact first-step value.
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.02f);
    env.SetDecayTime(0.02f);
    env.SetSustainLevel(q31(0.5f));
    env.Trigger();
    run_until(env, [&] { return env.GetState() == AdsrEnv::State::SUSTAIN; });

    AdsrEnv fresh;
    fresh.Init(kSampleRate);
    fresh.SetAttackTime(0.02f);
    fresh.SetDecayTime(0.02f);
    fresh.SetSustainLevel(q31(0.5f));
    fresh.Trigger();
    q31_t expected_first_step = fresh.Process();

    env.Trigger();
    q31_t after_retrigger = env.Process();
    ASSERT_EQ(after_retrigger, expected_first_step);
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::ATTACK);
}

TEST(AdsrEnv_HoldStateDelaysDecay)
{
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.01f);
    env.SetHoldTime(0.005f); // 5 samples at 1kHz
    env.Trigger();

    run_until(env, [&] { return env.GetOutput() == Q31_MAX; });
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::HOLD);

    // Should stay in HOLD for exactly hold_value_ (5) samples.
    for (int i = 0; i < 4; ++i)
    {
        env.Process();
        ASSERT_TRUE(env.GetState() == AdsrEnv::State::HOLD);
    }
    env.Process();
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::DECAY);
}

TEST(AdsrEnv_LoopingSkipsDecayAndCyclesAttackRelease)
{
    // With looping enabled and no hold, hitting Q31_MAX in ATTACK jumps
    // straight to RELEASE (decay/sustain are skipped entirely), and hitting
    // 0 in RELEASE jumps back to ATTACK — a free-running attack/release
    // envelope, usable as an LFO-ish modulation source.
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.01f);
    env.SetReleaseTime(0.01f);
    env.SetLooping(true);
    env.Trigger();

    run_until(env, [&] { return env.GetOutput() == Q31_MAX; });
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::RELEASE);

    run_until(env, [&] { return env.GetOutput() == 0; });
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::ATTACK);
}

TEST(AdsrEnv_ResetReturnsToIdleWithZeroOutput)
{
    AdsrEnv env;
    env.Init(kSampleRate);
    env.SetAttackTime(0.02f);
    env.Trigger();
    for (int i = 0; i < 5; ++i)
    {
        env.Process();
    }
    ASSERT_TRUE(env.GetOutput() > 0);

    env.Reset();
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::IDLE);
    ASSERT_EQ(env.GetOutput(), 0);
}
