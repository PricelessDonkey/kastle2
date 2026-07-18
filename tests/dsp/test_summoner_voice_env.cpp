#include "../harness.hpp"
#include "apps/Summoner/SummonerVoiceEnv.hpp"

using namespace kastle2;

namespace
{
constexpr float kSr = 44000.0f;

// Runs n samples with a fixed gate, returning the last output.
q31_t Run(SummonerVoiceEnv &e, const int n, const bool gate)
{
    q31_t out = 0;
    for (int i = 0; i < n; i++)
    {
        out = e.Process(gate);
    }
    return out;
}
}

TEST(SummonerVoiceEnv_GateHigh_HoldsAtSustainLevel)
{
    SummonerVoiceEnv e;
    e.Init(kSr);
    e.SetAttackTime(0.002f);
    e.SetDecayTime(0.05f); // fast decay so we'd be silent without the hold
    e.Trigger();

    // One second in, a plain AD envelope would long since be at zero.
    const q31_t held = Run(e, static_cast<int>(kSr), true);
    ASSERT_EQ(held, SummonerVoiceEnv::kSustainLevel);

    // And it stays there indefinitely while the gate is high.
    ASSERT_EQ(Run(e, static_cast<int>(kSr), true), SummonerVoiceEnv::kSustainLevel);
}

TEST(SummonerVoiceEnv_GateDrop_ReachesSilence)
{
    SummonerVoiceEnv e;
    e.Init(kSr);
    e.SetAttackTime(0.002f);
    e.SetDecayTime(0.1f);
    e.Trigger();
    Run(e, static_cast<int>(kSr * 0.5f), true); // settle into the hold

    // Release = decay time (0.1s). Allow 2x for slack.
    const q31_t out = Run(e, static_cast<int>(kSr * 0.2f), false);
    ASSERT_EQ(out, 0);
}

TEST(SummonerVoiceEnv_ReleaseIsGradualNotACut)
{
    SummonerVoiceEnv e;
    e.Init(kSr);
    e.SetAttackTime(0.002f);
    e.SetDecayTime(1.0f);
    e.Trigger();
    Run(e, static_cast<int>(kSr), true); // held at 60%

    // Halfway through the 1s release the ramp should be partway down, not 0 or 60%.
    const q31_t mid = Run(e, static_cast<int>(kSr * 0.5f), false);
    ASSERT_TRUE(mid > 0);
    ASSERT_TRUE(mid < SummonerVoiceEnv::kSustainLevel);
}

TEST(SummonerVoiceEnv_RetriggerMidRelease_TakesOver)
{
    SummonerVoiceEnv e;
    e.Init(kSr);
    e.SetAttackTime(0.002f);
    e.SetDecayTime(1.0f);
    e.Trigger();
    Run(e, static_cast<int>(kSr * 0.5f), true);
    Run(e, static_cast<int>(kSr * 0.3f), false); // partway into release

    e.Trigger();
    // Attack is 2ms; well within 10ms the retriggered envelope should be near max.
    const q31_t out = Run(e, static_cast<int>(kSr * 0.01f), false);
    ASSERT_TRUE(out > static_cast<q31_t>(Q31_MAX * 0.9f));
}

TEST(SummonerVoiceEnv_GateHighWithoutTrigger_StaysSilent)
{
    SummonerVoiceEnv e;
    e.Init(kSr);
    ASSERT_EQ(Run(e, static_cast<int>(kSr), true), 0);
}

TEST(SummonerVoiceEnv_GateHighAfterVoiceFullyDecayed_StaysSilent)
{
    SummonerVoiceEnv e;
    e.Init(kSr);
    e.SetAttackTime(0.002f);
    e.SetDecayTime(0.02f);
    e.Trigger();
    // Let the voice decay to silence with the gate LOW...
    Run(e, static_cast<int>(kSr * 0.5f), false);
    // ...then raising the gate must not resurrect it.
    ASSERT_EQ(Run(e, static_cast<int>(kSr * 0.1f), true), 0);
}

TEST(AdsrEnv_SetAttackTimeMidAttack_ContinuesFromCurrentValue)
{
    // Characterization for the attack knob (SHIFT+BANK+POT_4): changing the
    // attack time mid-attack must not reset or jump the output — the new rate
    // simply applies from the current value onward.
    AdsrEnv env;
    env.Init(kSr);
    env.SetSustainLevel(0);
    env.SetAttackTime(1.0f); // slow: far from max after 100ms
    env.Trigger();

    q31_t before = 0;
    for (int i = 0; i < static_cast<int>(kSr * 0.1f); i++)
    {
        before = env.Process();
    }
    ASSERT_TRUE(env.GetState() == AdsrEnv::State::ATTACK);
    ASSERT_TRUE(before > 0);
    ASSERT_TRUE(before < static_cast<q31_t>(Q31_MAX * 0.5f));

    env.SetAttackTime(0.001f);
    const q31_t just_after = env.Process();
    // Continuous: no reset to zero, still rising.
    ASSERT_TRUE(just_after >= before);

    // New (fast) rate applies immediately: max within 5ms.
    q31_t out = just_after;
    for (int i = 0; i < static_cast<int>(kSr * 0.005f); i++)
    {
        out = env.Process();
    }
    ASSERT_TRUE(out > static_cast<q31_t>(Q31_MAX * 0.95f));
}
