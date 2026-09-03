#include <cmath>
#include <cstdlib>
#include "../harness.hpp"
#include "apps/Summoner/SummonerNoiseTimbre.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"
#include "common/dsp/filters/Svf.hpp"

using namespace kastle2;

namespace
{
constexpr float kSr = 44000.0f;

q15_t Knob(const float frac)
{
    return static_cast<q15_t>(frac * Q15_MAX + 0.5f);
}

q15_t Clamp15(const int32_t v)
{
    if (v > Q15_MAX)
    {
        return Q15_MAX;
    }
    if (v < -Q15_MAX)
    {
        return -Q15_MAX;
    }
    return static_cast<q15_t>(v);
}

// Mirrors the app's per-sample noise-character path exactly: tilt one-pole
// crossfade, then threshold dust with make-up gain. Kept in the test so the RMS
// measurements below pin the real signal chain, not just the knob map.
float PipelineRms(const float knob, const int samples = 200000)
{
    const SummonerNoiseTimbre::Result r = SummonerNoiseTimbre::Compute(Knob(knob));
    const q15_t coef = SummonerNoiseTimbre::TiltCoef(kSr);

    WhiteNoise noise;
    noise.Seed(0x1234u);
    int32_t lp = 0;
    double sum = 0.0;

    for (int i = 0; i < samples; i++)
    {
        q15_t n = noise.Process();

        // Tilt: crossfade flat against the make-up-gained lowpass output.
        lp += ((static_cast<int32_t>(n) - lp) * coef) >> 15;
        if (r.tilt > 0)
        {
            const int32_t boosted = Clamp15(lp * SummonerNoiseTimbre::kLowpassMakeup);
            const int32_t mixed = (static_cast<int32_t>(n) * (Q15_MAX - r.tilt) + boosted * r.tilt) / Q15_MAX;
            n = Clamp15(mixed);
        }

        // Dust: keep only impulses above the threshold, with make-up gain.
        if (r.dust_thresh > 0)
        {
            n = (std::abs(static_cast<int32_t>(n)) >= r.dust_thresh)
                    ? Clamp15((static_cast<int32_t>(n) * r.dust_gain) >> 15)
                    : 0;
        }

        sum += static_cast<double>(n) * static_cast<double>(n);
    }
    return static_cast<float>(std::sqrt(sum / samples));
}
}

TEST(SummonerNoiseTimbre_FoldIsContinuousAtCenter)
{
    // 50% must be plain white — the current as-built sound — so both halves meet
    // there: flat tilt, full density, no resonance, fully per-voice.
    const SummonerNoiseTimbre::Result c = SummonerNoiseTimbre::Compute(Q15_HALF);
    ASSERT_EQ(c.tilt, 0);
    ASSERT_EQ(c.dust_thresh, 0);
    ASSERT_EQ(c.dust_gain, Q15_MAX);
    ASSERT_NEAR(c.resonance, 0.0f, 1e-6f);
    ASSERT_EQ(c.shared_mix, 0);

    // Approaching from either side converges on that same point.
    const SummonerNoiseTimbre::Result below = SummonerNoiseTimbre::Compute(Knob(0.49f));
    const SummonerNoiseTimbre::Result above = SummonerNoiseTimbre::Compute(Knob(0.51f));
    ASSERT_TRUE(below.tilt < Q15_MAX / 20);
    ASSERT_EQ(below.dust_thresh, 0);
    ASSERT_TRUE(above.tilt == 0);
    ASSERT_TRUE(above.dust_thresh < Q15_MAX / 5);
    ASSERT_TRUE(above.resonance < 0.1f);
    ASSERT_TRUE(above.shared_mix < Q15_MAX / 10);
}

TEST(SummonerNoiseTimbre_LowerHalfIsTiltOnly)
{
    // Below the fold nothing but the tilt moves: no dust, no resonance, and the
    // four per-voice streams stay independent (a wash wants width).
    q15_t prev = Q15_MAX + 1;
    for (float f = 0.0f; f <= 0.5f; f += 0.05f)
    {
        const SummonerNoiseTimbre::Result r = SummonerNoiseTimbre::Compute(Knob(f));
        ASSERT_EQ(r.dust_thresh, 0);
        ASSERT_EQ(r.dust_gain, Q15_MAX);
        ASSERT_NEAR(r.resonance, 0.0f, 1e-6f);
        ASSERT_EQ(r.shared_mix, 0);
        ASSERT_TRUE(r.tilt <= prev); // brown at 0%, monotonically flattening to white
        prev = r.tilt;
    }
    ASSERT_EQ(SummonerNoiseTimbre::Compute(0).tilt, Q15_MAX);
}

TEST(SummonerNoiseTimbre_UpperHalfCouplesDensityAndResonance)
{
    // The whole point of the upper half: density falls *while* Q rises, as one
    // gesture — sparse excitation is what makes the resonator ring instead of
    // smearing into hiss.
    q15_t prev_thresh = 0;
    float prev_res = -1.0f;
    q15_t prev_shared = 0;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const SummonerNoiseTimbre::Result r = SummonerNoiseTimbre::Compute(Knob(f));
        ASSERT_EQ(r.tilt, 0);
        ASSERT_TRUE(r.dust_thresh >= prev_thresh); // thinning
        ASSERT_TRUE(r.resonance >= prev_res);      // ringing harder
        ASSERT_TRUE(r.shared_mix >= prev_shared);  // collapsing to the shared stream
        prev_thresh = r.dust_thresh;
        prev_res = r.resonance;
        prev_shared = r.shared_mix;
    }
    const SummonerNoiseTimbre::Result top = SummonerNoiseTimbre::Compute(Q15_MAX);
    ASSERT_NEAR(top.resonance, SummonerNoiseTimbre::kMaxResonance, 1e-4f);
    ASSERT_EQ(top.shared_mix, Q15_MAX);
    ASSERT_TRUE(top.resonance < 1.0f); // Svf stability
}

TEST(SummonerNoiseTimbre_DustGainCompensatesButIsCapped)
{
    // Without compensation, thinning the density just fades the noise out, which
    // reads as a broken knob. With uncapped compensation the surviving impulses
    // (already near full scale) clip. Both bounds are load-bearing.
    ASSERT_EQ(SummonerNoiseTimbre::Compute(Q15_HALF).dust_gain, Q15_MAX);
    q15_t prev = 0;
    int32_t prev_gain = 0;
    for (float f = 0.5f; f <= 1.0f + 1e-4f; f += 0.05f)
    {
        const SummonerNoiseTimbre::Result r = SummonerNoiseTimbre::Compute(Knob(f));
        ASSERT_TRUE(r.dust_gain >= Q15_MAX);
        ASSERT_TRUE(r.dust_gain <= SummonerNoiseTimbre::kMaxDustGain);
        ASSERT_TRUE(r.dust_gain >= prev_gain); // sparser always needs more make-up
        prev_gain = r.dust_gain;
        prev = r.dust_thresh;
    }
    ASSERT_TRUE(prev > 0);
}

TEST(SummonerNoiseTimbre_LowerHalfHoldsItsLevel)
{
    // Brown must read as *body*, not as "quieter" — the lowpass make-up gain is
    // what makes the tilt a timbre control instead of a volume control. Measured
    // through the real WhiteNoise + one-pole path, held within ~3 dB of white.
    const float white = PipelineRms(0.5f);
    ASSERT_TRUE(white > 0.0f);
    for (float f = 0.0f; f <= 0.5f; f += 0.125f)
    {
        const float rms = PipelineRms(f);
        ASSERT_TRUE(rms > white * 0.7f);
        ASSERT_TRUE(rms < white * 1.42f);
    }
}

TEST(SummonerNoiseTimbre_UpperHalfThinsWithoutVanishing)
{
    // The sparse end is *meant* to drop in level (the gain cap is deliberate —
    // the resonator's ringing fills the gaps), but it must stay clearly present
    // rather than fading to nothing.
    const float white = PipelineRms(0.5f);
    const float mid = PipelineRms(0.75f);
    const float top = PipelineRms(1.0f);
    ASSERT_TRUE(mid < white);          // audibly thinning
    ASSERT_TRUE(mid > white * 0.4f);   // but not collapsing
    ASSERT_TRUE(top > white * 0.15f);  // still present at the very top
    ASSERT_TRUE(top < mid);
}

namespace
{
// The *shared* upper-half path end to end: threshold dust -> bandpass -> make-up.
// The original Phase 15 tests measured the dust and tilt paths but never this one,
// which is the stage that actually eats the signal — the resonator was ~30 dB down
// at a low chord root and the dust was inaudible on hardware (2026-09-03).
float SharedPathRms(const float knob, const float f0, const bool apply_makeup,
                    const int samples = 200000)
{
    const SummonerNoiseTimbre::Result r = SummonerNoiseTimbre::Compute(Knob(knob));
    const int32_t makeup =
        apply_makeup ? SummonerNoiseTimbre::ResonatorMakeup(r.upper_pos, f0, kSr) : 1;

    WhiteNoise noise;
    noise.Seed(0x5EEDBEEFu);
    Svf bp;
    bp.Init(kSr);
    bp.SetType(Svf::Type::BANDPASS);
    bp.SetFrequency(f0);
    bp.SetResonance(r.resonance, Svf::ForceValue::TRUE);

    double sum = 0.0;
    for (int i = 0; i < samples; i++)
    {
        int32_t v = noise.Process();
        if (r.dust_thresh > 0)
        {
            v = (std::abs(v) >= r.dust_thresh) ? Clamp15((v * r.dust_gain) >> 15) : 0;
        }
        const int32_t out = Clamp15(bp.Process(Clamp15(v)) * makeup);
        sum += static_cast<double>(out) * static_cast<double>(out);
    }
    return static_cast<float>(std::sqrt(sum / samples));
}
}

TEST(SummonerNoiseTimbre_ResonatorMakeupKeepsTheDustAudible)
{
    // Without the make-up the bandpass output is 20-30 dB below white — the bug
    // the first hardware listen caught. With it, the resonant stream must land in
    // the same ballpark as plain white at every chord root.
    const float white = PipelineRms(0.5f);
    for (const float f0 : {65.0f, 130.0f, 260.0f, 520.0f, 1040.0f})
    {
        const float raw = SharedPathRms(1.0f, f0, false);
        const float made_up = SharedPathRms(1.0f, f0, true);
        ASSERT_TRUE(raw < white * 0.1f);        // the loss is real
        ASSERT_TRUE(made_up > white * 0.2f);    // and the make-up recovers it
        ASSERT_TRUE(made_up < white * 1.2f);    // without overshooting into clipping
    }
}

TEST(SummonerNoiseTimbre_ResonatorLevelHoldsAcrossTheUpperHalf)
{
    // The top of the knob is meant to thin out, not disappear: the resonant
    // stream stays present all the way across the upper half at a typical root.
    const float white = PipelineRms(0.5f);
    for (float f = 0.6f; f <= 1.0f + 1e-4f; f += 0.1f)
    {
        const float rms = SharedPathRms(f, 130.0f, true);
        ASSERT_TRUE(rms > white * 0.2f);
        ASSERT_TRUE(rms < white * 1.2f);
    }
}

TEST(SummonerNoiseTimbre_ResonatorMakeupRisesAsTheRootFalls)
{
    // Lower roots need more make-up (the bandpass's sqrt(f0) energy law), and the
    // gain is bounded so it can never overflow the audio path.
    int32_t prev = 0;
    for (const float f0 : {1040.0f, 520.0f, 260.0f, 130.0f, 65.0f})
    {
        const int32_t g = SummonerNoiseTimbre::ResonatorMakeup(1.0f, f0, kSr);
        ASSERT_TRUE(g >= prev);
        ASSERT_TRUE(g >= 1);
        ASSERT_TRUE(g <= SummonerNoiseTimbre::kMaxResonatorMakeup);
        prev = g;
    }
    // Degenerate centre frequencies must not produce a garbage gain.
    ASSERT_TRUE(SummonerNoiseTimbre::ResonatorMakeup(1.0f, 0.0f, kSr) <=
                SummonerNoiseTimbre::kMaxResonatorMakeup);
}
