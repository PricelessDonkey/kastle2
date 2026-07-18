#include <cmath>
#include "../harness.hpp"
#include "prototypes/NoiseBlendCrossfade.hpp"
#include "common/dsp/synthesis/OscillatorQ15.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"

using namespace kastle2;
using namespace kastle2::prototype;

namespace
{
constexpr float kSampleRate = 44100.0f;

// Root-mean-square over a block, as a float in [0, 1] — a cheap stand-in
// for perceived loudness, good enough to compare the two blend laws.
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

TEST(NoiseBlendCrossfade_LinearMatchesEndpoints)
{
    q15_t dry = q15(0.6f);
    q15_t wet = q15(-0.4f);

    // q15_mult(x, Q15_MAX) isn't bit-exact x (the >>15 truncation loses an
    // LSB or two) — assert within a couple counts, not exact equality.
    ASSERT_NEAR(LinearBlend(dry, wet, Q15_ZERO), dry, 2);
    ASSERT_NEAR(q15_to_float(LinearBlend(dry, wet, Q15_MAX)), q15_to_float(wet), 0.001);
}

TEST(NoiseBlendCrossfade_EqualPowerMatchesEndpointsToo)
{
    q15_t dry = q15(0.6f);
    q15_t wet = q15(-0.4f);

    ASSERT_NEAR(q15_to_float(EqualPowerBlend(dry, wet, Q15_ZERO)), q15_to_float(dry), 0.001);
    ASSERT_NEAR(q15_to_float(EqualPowerBlend(dry, wet, Q15_MAX)), q15_to_float(wet), 0.001);
}

TEST(NoiseBlendCrossfade_LinearLawDipsInPowerAtMidpoint)
{
    // Q15_HALF is ~0.5 in this codebase's q15 convention (see qmath.hpp).
    float dry_gain = q15_to_float(LinearBlendGainDry(Q15_HALF));
    float wet_gain = q15_to_float(LinearBlendGainWet(Q15_HALF));
    float power = dry_gain * dry_gain + wet_gain * wet_gain;

    // Equal gains of 0.5 each sum-of-squares to 0.5, not 1.0 — a real
    // ~3dB dip (10*log10(0.5) ~= -3dB), not a rounding artifact.
    ASSERT_NEAR(power, 0.5, 0.02);
    ASSERT_TRUE(power < 0.9); // clearly not power-preserving
}

TEST(NoiseBlendCrossfade_EqualPowerMaintainsConstantPowerAcrossRange)
{
    const q15_t blends[] = {Q15_ZERO, Q15_MAX / 4, Q15_HALF, (3 * Q15_MAX) / 4, Q15_MAX};

    for (q15_t blend : blends)
    {
        float dry_gain = q15_to_float(EqualPowerBlendGainDry(blend));
        float wet_gain = q15_to_float(EqualPowerBlendGainWet(blend));
        float power = dry_gain * dry_gain + wet_gain * wet_gain;
        ASSERT_NEAR(power, 1.0, 0.02);
    }
}

TEST(NoiseBlendCrossfade_LinearRmsSweepDipsAtMidpointForRealSignals)
{
    // Blend a real oscillator against real (seeded) white noise, the way
    // CHORD-GEN's per-voice chain would, and measure RMS across a sweep —
    // confirms the dip survives contact with actual signals, not just the
    // gain-coefficient math above.
    OscillatorQ15 osc;
    osc.Init(kSampleRate);
    osc.SetWaveform(Oscillator::Waveform::SINE);
    osc.SetFrequency(220.0f);

    WhiteNoise noise;
    noise.Seed(12345);

    constexpr int kBlockSize = 512;
    q15_t buf[kBlockSize];

    auto render = [&](q15_t blend, bool equal_power) {
        for (int i = 0; i < kBlockSize; ++i)
        {
            q15_t o = osc.Process();
            q15_t n = noise.Process();
            buf[i] = equal_power ? EqualPowerBlend(o, n, blend) : LinearBlend(o, n, blend);
        }
        return Rms(buf, kBlockSize);
    };

    float linear_dry_rms = render(Q15_ZERO, false);
    float linear_mid_rms = render(Q15_HALF, false);
    float linear_wet_rms = render(Q15_MAX, false);

    float equal_power_dry_rms = render(Q15_ZERO, true);
    float equal_power_mid_rms = render(Q15_HALF, true);
    float equal_power_wet_rms = render(Q15_MAX, true);

    // Both laws should land near the same RMS at the endpoints (loosened
    // slightly beyond the coefficient-level tolerance above to absorb
    // accumulated per-sample q15_mult rounding over a 512-sample block).
    ASSERT_NEAR(linear_dry_rms, equal_power_dry_rms, 0.03);
    ASSERT_NEAR(linear_wet_rms, equal_power_wet_rms, 0.03);

    // ...but at the midpoint, the linear law's RMS should sag noticeably
    // below both endpoints, while equal-power's shouldn't sag nearly as
    // much. This is the audible "volume dip while turning the knob" the
    // design doc flags as the tradeoff for the cheaper linear law.
    float endpoint_avg = (linear_dry_rms + linear_wet_rms) / 2.0f;
    ASSERT_TRUE(linear_mid_rms < endpoint_avg * 0.85f);

    float equal_power_endpoint_avg = (equal_power_dry_rms + equal_power_wet_rms) / 2.0f;
    ASSERT_TRUE(equal_power_mid_rms > equal_power_endpoint_avg * 0.85f);
}

TEST(NoiseBlendCrossfade_ZeroBlendIsPureOscillatorNoNoiseLeakage)
{
    // Sanity check on the mixing itself (not the law): at blend=0 the
    // noise source must contribute nothing, under either law.
    OscillatorQ15 osc_a, osc_b;
    osc_a.Init(kSampleRate);
    osc_b.Init(kSampleRate);
    osc_a.SetFrequency(440.0f);
    osc_b.SetFrequency(440.0f);

    WhiteNoise noise;
    noise.Seed(999);

    for (int i = 0; i < 64; ++i)
    {
        q15_t o_a = osc_a.Process();
        q15_t o_b = osc_b.Process();
        q15_t n = noise.Process();

        ASSERT_NEAR(LinearBlend(o_a, n, Q15_ZERO), o_a, 2);
        ASSERT_NEAR(q15_to_float(EqualPowerBlend(o_b, n, Q15_ZERO)), q15_to_float(o_b), 0.001);
    }
}
