#include <cmath>
#include "../harness.hpp"
#include "common/dsp/synthesis/NoiseBlend.hpp"
#include "common/dsp/synthesis/OscillatorQ15.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"

using namespace kastle2;

namespace
{
constexpr float kSampleRate = 44100.0f;

// Root-mean-square over a block, as a float in [0, 1] — a cheap stand-in
// for perceived loudness.
float Rms(const q15_t *buf, int n)
{
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double s = q15_to_float(buf[i]);
        sum_sq += s * s;
    }
    return static_cast<float>(std::sqrt(sum_sq / n));
}
} // namespace

TEST(NoiseBlend_MatchesEndpoints)
{
    q15_t dry = q15(0.6f);
    q15_t wet = q15(-0.4f);

    ASSERT_NEAR(q15_to_float(NoiseBlend(dry, wet, Q15_ZERO)), q15_to_float(dry), 0.001);
    ASSERT_NEAR(q15_to_float(NoiseBlend(dry, wet, Q15_MAX)), q15_to_float(wet), 0.001);
}

TEST(NoiseBlend_MaintainsConstantPowerAcrossRange)
{
    // Chosen 2026-07-21 over the linear law specifically because squared
    // gains sum to ~1.0 across the whole sweep instead of dipping to 0.5
    // (~3dB) at the midpoint. Prototype comparison lived in
    // tests/dsp/prototypes/NoiseBlendCrossfade.hpp; deleted now that the
    // law is decided and promoted into common/dsp/synthesis/.
    const q15_t blends[] = {Q15_ZERO, Q15_MAX / 4, Q15_HALF, (3 * Q15_MAX) / 4, Q15_MAX};

    for (q15_t blend : blends)
    {
        float dry_gain = q15_to_float(NoiseBlendGainDry(blend));
        float wet_gain = q15_to_float(NoiseBlendGainWet(blend));
        float power = dry_gain * dry_gain + wet_gain * wet_gain;
        ASSERT_NEAR(power, 1.0, 0.02);
    }
}

TEST(NoiseBlend_RmsSweepHasNoMidpointDipForRealSignals)
{
    // Blend a real oscillator against real (seeded) white noise, the way
    // CHORD-GEN's per-voice chain would, and measure RMS across a sweep.
    OscillatorQ15 osc;
    osc.Init(kSampleRate);
    osc.SetWaveform(Oscillator::Waveform::SINE);
    osc.SetFrequency(220.0f);

    WhiteNoise noise;
    noise.Seed(12345);

    constexpr int kBlockSize = 512;
    q15_t buf[kBlockSize];

    auto render = [&](q15_t blend) {
        for (int i = 0; i < kBlockSize; ++i)
        {
            q15_t o = osc.Process();
            q15_t n = noise.Process();
            buf[i] = NoiseBlend(o, n, blend);
        }
        return Rms(buf, kBlockSize);
    };

    float dry_rms = render(Q15_ZERO);
    float mid_rms = render(Q15_HALF);
    float wet_rms = render(Q15_MAX);

    float endpoint_avg = (dry_rms + wet_rms) / 2.0f;
    ASSERT_TRUE(mid_rms > endpoint_avg * 0.85f);
}

TEST(NoiseBlend_ZeroBlendIsPureOscillatorNoNoiseLeakage)
{
    OscillatorQ15 osc;
    osc.Init(kSampleRate);
    osc.SetFrequency(440.0f);

    WhiteNoise noise;
    noise.Seed(999);

    for (int i = 0; i < 64; ++i)
    {
        q15_t o = osc.Process();
        q15_t n = noise.Process();
        ASSERT_NEAR(q15_to_float(NoiseBlend(o, n, Q15_ZERO)), q15_to_float(o), 0.001);
    }
}
