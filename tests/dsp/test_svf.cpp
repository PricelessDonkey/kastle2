#include <cmath>
#include "../harness.hpp"
#include "common/dsp/filters/Svf.hpp"

using namespace kastle2;

namespace
{
constexpr float kSr = 44000.0f;

// RMS of the filter response to a sine of the given frequency (post-warmup).
float ResponseRms(Svf &f, const float freq, const int warmup = 2000, const int measure = 4000)
{
    double sum = 0.0;
    for (int i = 0; i < warmup + measure; i++)
    {
        const float phase = 2.0f * static_cast<float>(M_PI) * freq * static_cast<float>(i) / kSr;
        const q15_t in = static_cast<q15_t>(std::sin(phase) * 0.5f * Q15_MAX);
        const q15_t out = f.Process(in);
        if (i >= warmup)
        {
            sum += static_cast<double>(out) * static_cast<double>(out);
        }
    }
    return static_cast<float>(std::sqrt(sum / measure));
}
}

TEST(Svf_LowpassPassesLowAndCutsHigh)
{
    Svf f;
    f.Init(kSr);
    f.SetType(Svf::Type::LOWPASS);
    f.SetFrequency(1000.0f);
    f.SetResonance(0.1f);

    Svf f2;
    f2.Init(kSr);
    f2.SetType(Svf::Type::LOWPASS);
    f2.SetFrequency(1000.0f);
    f2.SetResonance(0.1f);

    const float low = ResponseRms(f, 100.0f);
    const float high = ResponseRms(f2, 8000.0f);

    // 100 Hz through a 1 kHz LP is nearly untouched; 8 kHz is well down.
    ASSERT_TRUE(low > 4.0f * high);
    ASSERT_TRUE(low > 0.3f * 0.5f * Q15_MAX); // in the ballpark of the input level
}

TEST(Svf_HighpassPassesHighAndCutsLow)
{
    Svf f;
    f.Init(kSr);
    f.SetType(Svf::Type::HIGHPASS);
    f.SetFrequency(1000.0f);
    f.SetResonance(0.1f);

    Svf f2;
    f2.Init(kSr);
    f2.SetType(Svf::Type::HIGHPASS);
    f2.SetFrequency(1000.0f);
    f2.SetResonance(0.1f);

    const float high = ResponseRms(f, 8000.0f);
    const float low = ResponseRms(f2, 100.0f);

    ASSERT_TRUE(high > 4.0f * low);
}

TEST(Svf_BandpassPeaksAtCutoff)
{
    Svf a;
    a.Init(kSr);
    a.SetType(Svf::Type::BANDPASS);
    a.SetFrequency(1000.0f);
    a.SetResonance(0.2f);

    Svf b;
    b.Init(kSr);
    b.SetType(Svf::Type::BANDPASS);
    b.SetFrequency(1000.0f);
    b.SetResonance(0.2f);

    Svf c;
    c.Init(kSr);
    c.SetType(Svf::Type::BANDPASS);
    c.SetFrequency(1000.0f);
    c.SetResonance(0.2f);

    const float at_cutoff = ResponseRms(a, 1000.0f);
    const float below = ResponseRms(b, 100.0f);
    const float above = ResponseRms(c, 8000.0f);

    ASSERT_TRUE(at_cutoff > below);
    ASSERT_TRUE(at_cutoff > above);
}

TEST(Svf_BypassHalvesTheInput)
{
    // Characterization, not a spec: BYPASS returns the internal downsampled
    // input (`in >> kDownsample`, kDownsample = 1) without the `<< (kDownsample
    // - 1)` output upscale the filtered taps get — so "bypass" is 6 dB down,
    // the same level as the filter outputs (all halved by design, per the
    // "dividing by two" comment in Svf.cpp). Don't A/B bypass against dry
    // input in an app without compensating.
    Svf f;
    f.Init(kSr);
    f.SetType(Svf::Type::BYPASS);
    f.SetFrequency(500.0f);

    for (q15_t in : {static_cast<q15_t>(0), static_cast<q15_t>(12345),
                     static_cast<q15_t>(-12346), static_cast<q15_t>(Q15_MAX)})
    {
        ASSERT_EQ(f.Process(in), static_cast<q15_t>(in >> 1));
    }
}

TEST(Svf_TypeGetterMatchesSetter)
{
    Svf f;
    f.Init(kSr);
    f.SetType(Svf::Type::NOTCH);
    ASSERT_TRUE(f.GetType() == Svf::Type::NOTCH);
}

TEST(Svf_OutputStaysInQ15RangeWithHighResonance)
{
    Svf f;
    f.Init(kSr);
    f.SetType(Svf::Type::LOWPASS);
    f.SetFrequency(2000.0f);
    f.SetResonance(0.9f);
    f.SetDrive(0.5f);

    // Full-scale square-ish input, worst case for a resonant filter.
    for (int i = 0; i < 20000; i++)
    {
        const q15_t in = (i / 20 % 2 == 0) ? static_cast<q15_t>(Q15_MAX)
                                           : static_cast<q15_t>(-Q15_MAX);
        const q15_t out = f.Process(in);
        ASSERT_TRUE(out <= Q15_MAX);
        ASSERT_TRUE(out >= -Q15_MAX - 1);
    }
}
