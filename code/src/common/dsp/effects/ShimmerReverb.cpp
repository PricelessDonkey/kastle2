#include "ShimmerReverb.hpp"

using namespace kastle2;

namespace
{
// (1 - pitch_ratio) in 1/256-sample units — the per-sample drift of the grain
// read delay. Positive step = read delay grows = pitch down; negative = up.
int32_t IntervalStep(ShimmerReverb::Interval interval)
{
    switch (interval)
    {
    case ShimmerReverb::Interval::OCTAVE_DOWN:
        return 128; // ratio 0.5
    case ShimmerReverb::Interval::FIFTH_UP:
        return -128; // ratio 1.498
    case ShimmerReverb::Interval::OCTAVE_UP:
        return -256; // ratio 2.0
    case ShimmerReverb::Interval::TWO_OCTAVE:
        return -768; // ratio 4.0
    default:
        return -128;
    }
}
} // namespace

int32_t ShimmerReverb::ModOffset(uint32_t phase)
{
    // Triangle from a phase accumulator: |saw| gives a 0..2^31..0 ramp over one
    // period; centre and scale it to ±kModDepth samples.
    int64_t saw = static_cast<int32_t>(phase);
    int64_t tri = saw < 0 ? -saw : saw;      // 0 .. 2^31
    int64_t centered = tri - (1LL << 30);    // -2^30 .. +2^30
    return static_cast<int32_t>((centered * static_cast<int64_t>(kModDepth)) >> 30);
}

void ShimmerReverb::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    lfo_inc_ = static_cast<uint32_t>((static_cast<double>(kModRateHz) / sample_rate) * 4294967296.0);
    SetDecay(0.85f);
    SetShimmer(0);
    SetDamping(0.2f);
    SetInterval(Interval::FIFTH_UP);
    Reset();
}

void ShimmerReverb::Reset()
{
    fb_l_ = 0;
    fb_r_ = 0;
    lfo_phase_ = 0;
    pre_delay_.Clear();
    diff0_.Clear();
    diff1_.Clear();
    diff2_.Clear();
    diff3_.Clear();
    tank_l_.Clear();
    tank_r_.Clear();
    ap_l1_.Clear();
    ap_l2_.Clear();
    ap_r1_.Clear();
    ap_r2_.Clear();
    damp_l_.Clear();
    damp_r_.Clear();
    pitch_.Clear();
    pitch_.base_step_ = pitch_step_;
}

void ShimmerReverb::SetDecay(float decay)
{
    if (decay < 0.0f)
    {
        decay = 0.0f;
    }
    if (decay > kMaxDecay)
    {
        decay = kMaxDecay;
    }
    decay_ = float_to_q15(decay);
}

void ShimmerReverb::SetShimmer(q15_t shimmer)
{
    shimmer_ = q15_saturate(shimmer < 0 ? 0 : shimmer);
}

void ShimmerReverb::SetDamping(float damping)
{
    if (damping < 0.0f)
    {
        damping = 0.0f;
    }
    if (damping > 1.0f)
    {
        damping = 1.0f;
    }
    // coef = 1 - damping: full damping → heavy HF rolloff, 0 → pass-through.
    q15_t coef = float_to_q15(1.0f - damping);
    damp_l_.coef_ = coef;
    damp_r_.coef_ = coef;
}

void ShimmerReverb::SetInterval(Interval interval)
{
    pitch_step_ = IntervalStep(interval);
    pitch_.base_step_ = pitch_step_;
}

FASTCODE ShimmerReverb::Output ShimmerReverb::Process(q15_t input)
{
    // Pre-delay → 4× input diffusion.
    q15_t mono = pre_delay_.Process(input, kPreDelayTap);
    mono = diff0_.Process(mono, kInDiffA);
    mono = diff1_.Process(mono, kInDiffA);
    mono = diff2_.Process(mono, kInDiffB);
    mono = diff3_.Process(mono, kInDiffB);

    // Tank modulation offsets (quadrature so the two halves decorrelate).
    int32_t mod_l = ModOffset(lfo_phase_);
    int32_t mod_r = ModOffset(lfo_phase_ + 0x40000000u);
    lfo_phase_ += lfo_inc_;

    // Left half: input + feedback crossed from the right half.
    q15_t lx = tank_l_.Process(q15_saturate(mono + fb_r_),
                               static_cast<std::size_t>(static_cast<int32_t>(kTankDelay) + mod_l));
    lx = ap_l1_.Process(lx, kTankDiffA);
    lx = ap_l2_.Process(lx, kTankDiffB);
    lx = damp_l_.Process(lx);
    q15_t tap_l = lx;
    q15_t fb_l_raw = q15_mult(lx, decay_);

    // Right half: input + feedback crossed from the left half.
    q15_t rx = tank_r_.Process(q15_saturate(mono + fb_l_),
                               static_cast<std::size_t>(static_cast<int32_t>(kTankDelay) + mod_r));
    rx = ap_r1_.Process(rx, kTankDiffA);
    rx = ap_r2_.Process(rx, kTankDiffB);
    rx = damp_r_.Process(rx);
    q15_t tap_r = rx;
    q15_t fb_r_raw = q15_mult(rx, decay_);

    // Shimmer: one mono pitch shifter on the summed feedback; blend the shifted
    // signal into each half's feedback by the shimmer amount.
    // Granular extremes (opt-in, off by default): ramp the grain shrink + jitter
    // in across the top of the shimmer range (bit-identical below kExtremeStart).
    q15_t extreme = (extreme_enabled_ && shimmer_ > kExtremeStart)
                        ? q15_saturate((shimmer_ - kExtremeStart) * 5)
                        : 0;
    pitch_.SetExtreme(extreme);

    q15_t shimmer_in = (fb_l_raw + fb_r_raw) >> 1;
    q15_t shifted = pitch_.Process(shimmer_in);
    fb_l_ = q15_saturate(fb_l_raw + q15_mult(shimmer_, shifted - fb_l_raw));
    fb_r_ = q15_saturate(fb_r_raw + q15_mult(shimmer_, shifted - fb_r_raw));

    return Output{tap_l, tap_r};
}
