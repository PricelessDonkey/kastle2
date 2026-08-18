#pragma once

#include <cstddef>
#include <cstdint>

#include "common/dsp/math/qmath.hpp"
#include "common/fastcode.hpp"

namespace kastle2
{

/**
 * @class ShimmerReverb
 * @ingroup dsp_effects
 * @brief Dattorro-style plate reverb with a granular pitch shifter in the feedback loop.
 * @author Summoner (CHORD-GEN) build session
 * @date 2026-08-13
 *
 * A reduced Dattorro (1997) plate: pre-delay → 4× input-diffusion allpass →
 * two mirrored, cross-coupled tank halves (modulated delay → 2 allpass →
 * damping LP). A two-grain granular pitch shifter runs on the tank feedback;
 * @ref SetShimmer blends the shifted signal back into the tank so each pass
 * rises in pitch and accumulates into a shimmering halo (0 = plain plate,
 * Q15_MAX = infinite shimmer).
 *
 * All state is statically allocated (fixed-size q15least_t buffers, ~28KB) and
 * the recursion runs in q15_t arithmetic — the precision prototype
 * (tests/dsp/prototypes/AllpassPrecision.hpp) confirmed Q15 storage + Q15 math
 * is adequate; see REVERB.md's resolved-questions section.
 *
 * @note Single mono pitch shifter (fed by the summed tank feedback) for now;
 *       the stereo-shifter question in REVERB.md stays open.
 * @note Granular-extremes behaviour (top of shimmer range) is opt-in via
 *       @ref SetExtremeEnabled and off by default (2026-08-18) — the default
 *       is the fixed ~60ms-grain baseline.
 */
class ShimmerReverb
{
public:
    /** @brief Stereo wet output sample pair. */
    struct Output
    {
        q15_t left;  ///< Left channel wet sample
        q15_t right; ///< Right channel wet sample
    };

    /** @brief Shimmer pitch-shift interval. */
    enum class Interval
    {
        OCTAVE_DOWN, ///< -12 semitones (dark shimmer, ratio 0.5)
        FIFTH_UP,    ///< +7 semitones (default, warmer, ratio 1.498)
        OCTAVE_UP,   ///< +12 semitones (classic shimmer, ratio 2.0)
        TWO_OCTAVE,  ///< +24 semitones (extreme, ratio 4.0)
        COUNT
    };

    ShimmerReverb() = default;

    /**
     * @brief Initialize buffers and set musical defaults.
     * @param sample_rate The audio sample rate in Hz.
     */
    void Init(float sample_rate);

    /** @brief Clear all delay/allpass/grain state (silence the tail). */
    void Reset();

    /**
     * @brief Process one mono input sample.
     * @param input Mono input sample (sum the app's L/R before calling).
     * @return Stereo wet output (two tank taps).
     */
    FASTCODE Output Process(q15_t input);

    /**
     * @brief Set tank feedback gain — how long the tail rings.
     * @param decay 0.0 (dead) to ~1.0 (infinite); clamped to kMaxDecay.
     */
    void SetDecay(float decay);

    /**
     * @brief Set shimmer amount — how much pitch-shifted signal enters feedback.
     * @param shimmer 0 (plain plate) to Q15_MAX (full shimmer). Read every sample.
     */
    void SetShimmer(q15_t shimmer);

    /**
     * @brief Set HF damping in the feedback path.
     * @param damping 0.0 (bright) to 1.0 (dark); 1-pole LP rolloff.
     */
    void SetDamping(float damping);

    /** @brief Set the shimmer pitch-shift interval. */
    void SetInterval(Interval interval);

    /**
     * @brief Enable/disable the granular extremes at the top of the shimmer range.
     * @param enabled true = grain shrink + jitter ramp in above 80% shimmer;
     *                false (the default) = fixed ~60ms grain at every shimmer
     *                amount, bit-identical to the plain shifter.
     *
     * Off by default since 2026-08-18: Summoner's shimmer knob is a plain
     * 0->full ramp with no chattering zone (CHORD-GEN Phase 12). Berserker
     * still wants the extremes, so the behaviour is opt-in rather than deleted.
     */
    void SetExtremeEnabled(bool enabled) { extreme_enabled_ = enabled; }

    /** @brief Diagnostic (tests): total granular grain wraps since Init/Reset. */
    uint32_t DebugGrainWraps() const { return pitch_.wraps_; }

    // ---- Fixed buffer sizes (44kHz-tuned; total ~28KB) ----
    static constexpr std::size_t kPreDelayBuf = 1100;   ///< 25ms max pre-delay
    static constexpr std::size_t kPreDelayTap = 528;    ///< 12ms fixed pre-delay
    static constexpr std::size_t kDiff0 = 210;
    static constexpr std::size_t kDiff1 = 158;
    static constexpr std::size_t kDiff2 = 561;
    static constexpr std::size_t kDiff3 = 410;
    static constexpr std::size_t kTankDelay = 2640;     ///< main tank delay (per half)
    static constexpr std::size_t kTankBuf = 2660;       ///< tank delay buffer (delay + mod headroom)
    static constexpr std::size_t kApL1 = 967;
    static constexpr std::size_t kApL2 = 671;
    static constexpr std::size_t kApR1 = 1153;
    static constexpr std::size_t kApR2 = 773;
    static constexpr std::size_t kGrain = 2646;         ///< ~60ms grain buffer
    static constexpr std::size_t kModDepth = 4;         ///< tank delay modulation ±samples

    static constexpr float kMaxDecay = 0.97f; ///< clamp so the loop stays bounded

private:
    /** @brief Fixed-length Schroeder allpass diffuser (delay == N). */
    template <std::size_t N>
    struct Allpass
    {
        q15least_t buf_[N] = {};
        std::size_t pos_ = 0;

        q15_t Process(q15_t x, q15_t g)
        {
            q15_t delayed = buf_[pos_];
            q15_t w = q15_saturate(x + q15_mult(g, delayed));
            q15_t y = q15_saturate(delayed - q15_mult(g, w));
            buf_[pos_] = static_cast<q15least_t>(w);
            pos_ = (pos_ + 1) % N;
            return y;
        }

        void Clear()
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                buf_[i] = 0;
            }
            pos_ = 0;
        }
    };

    /** @brief Circular delay line; read at a runtime depth, then write. */
    template <std::size_t N>
    struct DelayLine
    {
        q15least_t buf_[N] = {};
        std::size_t pos_ = 0;

        q15_t Process(q15_t x, std::size_t depth)
        {
            std::size_t rp = (pos_ + N - depth) % N;
            q15_t y = buf_[rp];
            buf_[pos_] = static_cast<q15least_t>(x);
            pos_ = (pos_ + 1) % N;
            return y;
        }

        void Clear()
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                buf_[i] = 0;
            }
            pos_ = 0;
        }
    };

    /** @brief 1-pole lowpass damping filter. State-only, coefficient set externally. */
    struct Damper
    {
        q15_t state_ = 0;
        q15_t coef_ = Q15_MAX; ///< 1 - damping; Q15_MAX = no damping

        q15_t Process(q15_t x)
        {
            state_ = q15_saturate(state_ + q15_mult(coef_, x - state_));
            return state_;
        }

        void Clear() { state_ = 0; }
    };

    /**
     * @brief Two-grain granular pitch shifter over a single ~60ms buffer.
     *
     * Write pointer advances 1 sample/sample; each grain carries a fixed-point
     * phase (units of 1/kScale sample) that advances by @ref step_ so its read
     * delay drifts, yielding the pitch shift. The two grains are offset by half
     * the (effective) grain window and crossfade with complementary triangular
     * windows (unity sum, no click at wrap).
     *
     * Granular extremes (REVERB.md, off unless @ref ShimmerReverb::SetExtremeEnabled
     * turns them on): @ref SetExtreme drives, with a single
     * "extreme" amount, (a) the effective grain window shrinking from ~60ms
     * toward ~16ms — the read modulus drops, the buffer is never reallocated —
     * and (b) per-grain-wrap jitter on the read-start position and the pitch
     * step. Extreme 0 = fixed full-length grain, no jitter (bit-identical to the
     * plain shifter). Jitter uses a per-instance LCG, so no shared global RNG.
     */
    template <std::size_t N>
    struct PitchShifter
    {
        static constexpr int32_t kScale = 256;
        static constexpr int32_t kFullLen = static_cast<int32_t>(N);
        static constexpr int32_t kMinLen = 700;      ///< ~16ms shortest window
        static constexpr int32_t kMaxPosJit = 220;   ///< ±5ms read-start jitter
        static constexpr int32_t kMaxStepJit = 12;   ///< ±~5% pitch-ratio wobble

        q15least_t buf_[N] = {};
        std::size_t wpos_ = 0;
        int32_t base_step_ = 0; ///< (1 - pitch_ratio) in 1/kScale-sample units

        int32_t glen_ = kFullLen; ///< effective grain length (samples)
        int32_t range_ = kFullLen * kScale;
        int32_t pos_jit_max_ = 0;
        int32_t step_jit_max_ = 0;

        int32_t phase_[2] = {0, 0};
        int32_t pos_jit_[2] = {0, 0};
        int32_t step_jit_[2] = {0, 0};
        uint32_t rng_ = 0x2545F491u;
        uint32_t wraps_ = 0; ///< diagnostic: total grain wraps (test hook)

        int32_t NextRand(int32_t bound) // uniform in [-bound, bound]
        {
            if (bound <= 0)
            {
                return 0;
            }
            rng_ = rng_ * 1664525u + 1013904223u;
            return static_cast<int32_t>((rng_ >> 8) % static_cast<uint32_t>(2 * bound + 1)) - bound;
        }

        q15_t ReadGrain(int i) const
        {
            int32_t d = phase_[i] / kScale + pos_jit_[i]; // jittered read delay
            if (d < 0)
            {
                d = 0;
            }
            if (d >= kFullLen)
            {
                d = kFullLen - 1;
            }
            std::size_t rp = (wpos_ + N - static_cast<std::size_t>(d)) % N;
            q15_t s = buf_[rp];
            // Triangular window over [0, glen_): peak at glen_/2, zero at ends.
            int32_t half = glen_ / 2;
            int32_t gd = phase_[i] / kScale;
            int32_t dist = gd - half;
            if (dist < 0)
            {
                dist = -dist;
            }
            q15_t win = Q15_MAX - static_cast<q15_t>((static_cast<int64_t>(dist) * Q15_MAX) / half);
            if (win < 0)
            {
                win = 0;
            }
            return q15_mult(s, win);
        }

        void Advance(int i)
        {
            phase_[i] += base_step_ + step_jit_[i];
            bool wrapped = false;
            while (phase_[i] >= range_)
            {
                phase_[i] -= range_;
                wrapped = true;
            }
            while (phase_[i] < 0)
            {
                phase_[i] += range_;
                wrapped = true;
            }
            if (wrapped)
            {
                ++wraps_;
                pos_jit_[i] = NextRand(pos_jit_max_);
                step_jit_[i] = NextRand(step_jit_max_);
            }
        }

        q15_t Process(q15_t x)
        {
            buf_[wpos_] = static_cast<q15least_t>(x);
            q15_t y = q15_saturate(ReadGrain(0) + ReadGrain(1));
            wpos_ = (wpos_ + 1) % N;
            Advance(0);
            Advance(1);
            return y;
        }

        // extreme: 0 = full ~60ms grain, no jitter; Q15_MAX = shortest + full jitter.
        void SetExtreme(q15_t extreme)
        {
            glen_ = kFullLen - static_cast<int32_t>((static_cast<int64_t>(kFullLen - kMinLen) * extreme) / Q15_MAX);
            range_ = glen_ * kScale;
            pos_jit_max_ = static_cast<int32_t>((static_cast<int64_t>(kMaxPosJit) * extreme) / Q15_MAX);
            step_jit_max_ = static_cast<int32_t>((static_cast<int64_t>(kMaxStepJit) * extreme) / Q15_MAX);
            for (int i = 0; i < 2; ++i)
            {
                while (phase_[i] >= range_)
                {
                    phase_[i] -= range_;
                }
            }
        }

        void Clear()
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                buf_[i] = 0;
            }
            wpos_ = 0;
            glen_ = kFullLen;
            range_ = kFullLen * kScale;
            pos_jit_max_ = 0;
            step_jit_max_ = 0;
            phase_[0] = 0;
            phase_[1] = range_ / 2;
            pos_jit_[0] = pos_jit_[1] = 0;
            step_jit_[0] = step_jit_[1] = 0;
            wraps_ = 0;
            rng_ = 0x2545F491u;
        }
    };

    /** @brief Triangle tank-modulation offset in samples for a phase accumulator. */
    static int32_t ModOffset(uint32_t phase);

    float sample_rate_ = 44000.0f;
    q15_t decay_ = 0;
    q15_t shimmer_ = 0;
    int32_t pitch_step_ = 0;

    uint32_t lfo_phase_ = 0;
    uint32_t lfo_inc_ = 0;

    // Cross-coupled feedback signals (one sample of state between the halves).
    q15_t fb_l_ = 0;
    q15_t fb_r_ = 0;

    DelayLine<kPreDelayBuf> pre_delay_;
    Allpass<kDiff0> diff0_;
    Allpass<kDiff1> diff1_;
    Allpass<kDiff2> diff2_;
    Allpass<kDiff3> diff3_;

    DelayLine<kTankBuf> tank_l_;
    DelayLine<kTankBuf> tank_r_;
    Allpass<kApL1> ap_l1_;
    Allpass<kApL2> ap_l2_;
    Allpass<kApR1> ap_r1_;
    Allpass<kApR2> ap_r2_;
    Damper damp_l_;
    Damper damp_r_;

    PitchShifter<kGrain> pitch_;

    // Input diffusion / tank allpass coefficients (Dattorro-ish).
    static constexpr q15_t kInDiffA = q15(0.75f);
    static constexpr q15_t kInDiffB = q15(0.625f);
    static constexpr q15_t kTankDiffA = q15(0.7f);
    static constexpr q15_t kTankDiffB = q15(0.5f);
    static constexpr float kModRateHz = 0.5f;

    // Granular extremes ramp in only across the top of the shimmer range, and
    // only when explicitly enabled (off by default, 2026-08-18):
    // shimmer <= kExtremeStart is bit-identical to the plain shifter.
    static constexpr q15_t kExtremeStart = q15(0.8f);
    bool extreme_enabled_ = false;
};

} // namespace kastle2
