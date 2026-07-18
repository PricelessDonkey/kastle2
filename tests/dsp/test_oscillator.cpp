#include "../harness.hpp"
#include "common/dsp/synthesis/Oscillator.hpp"

using namespace kastle2;

namespace
{
constexpr float kSampleRate = 8000.0f;
constexpr float kFrequency = 1000.0f; // ~8 samples/cycle at this sample rate
} // namespace

TEST(Oscillator_DefaultsToRisingSineWithFullAmplitude)
{
    Oscillator osc;
    osc.Init(kSampleRate);
    ASSERT_TRUE(osc.IsRising());
    ASSERT_FALSE(osc.IsFalling());
}

TEST(Oscillator_SquareWaveAlternatesAtFiftyPercentDuty)
{
    // Default pulse_width_ is 0 (Q31_ZERO), and phase starts at Q31_MIN
    // (negative) — so the very first sample of a square wave should be
    // Q31_MAX, then flip to Q31_MIN once phase crosses zero.
    Oscillator osc;
    osc.Init(kSampleRate);
    osc.SetWaveform(Oscillator::Waveform::SQUARE);
    osc.SetFrequency(kFrequency);

    q31_t first = osc.Process();
    ASSERT_EQ(first, Q31_MAX);

    bool saw_low = false;
    for (int i = 0; i < static_cast<int>(osc.GetTicks()) + 2; ++i)
    {
        if (osc.Process() == Q31_MIN)
        {
            saw_low = true;
            break;
        }
    }
    ASSERT_TRUE(saw_low);
}

TEST(Oscillator_EndOfCycleResetsElapsedTicks)
{
    Oscillator osc;
    osc.Init(kSampleRate);
    osc.SetFrequency(kFrequency);

    bool saw_eoc = false;
    // Run a few cycles' worth of samples looking for a wrap.
    for (int i = 0; i < static_cast<int>(osc.GetTicks()) * 3 + 5; ++i)
    {
        osc.Process();
        if (osc.IsEOC())
        {
            saw_eoc = true;
            ASSERT_EQ(osc.GetElapsedTicks(), 0u);
            break;
        }
    }
    ASSERT_TRUE(saw_eoc);
}

TEST(Oscillator_AmplitudeScalesSquareWaveOutput)
{
    Oscillator osc;
    osc.Init(kSampleRate);
    osc.SetWaveform(Oscillator::Waveform::SQUARE);
    osc.SetFrequency(kFrequency);
    osc.SetAmplitude(q31(0.5f));

    q31_t first = osc.Process();
    // Half amplitude applied to what would otherwise be Q31_MAX.
    ASSERT_NEAR(q31_to_float(first), 0.5, 0.01);
}

TEST(Oscillator_ResetSetsPhaseDirectly)
{
    Oscillator osc;
    osc.Init(kSampleRate);
    osc.Reset(Q31_ZERO);
    ASSERT_EQ(osc.GetPhase(), Q31_ZERO);
    ASSERT_FALSE(osc.IsRising()); // phase >= 0 is the "falling" half per IsRising()'s definition
}

TEST(Oscillator_PhaseAddOffsetsPhaseForFm)
{
    Oscillator osc;
    osc.Init(kSampleRate);
    osc.Reset(Q31_ZERO);
    osc.PhaseAdd(1000);
    ASSERT_EQ(osc.GetPhase(), 1000);
}
