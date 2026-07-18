#pragma once

#include <cstdint>
#include "common/dsp/control/AdsrEnv.hpp"
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class SummonerVoiceEnv
 * @ingroup apps
 * @brief Per-voice envelope for Summoner: AdsrEnv in pure-AD mode plus an app-side sustain hold / release ramp.
 * @author sam
 * @date 2026-07-17
 *
 * AdsrEnv has no note-off: RELEASE is only reachable via looping, and SUSTAIN is
 * a terminal state (SetSustainLevel() alone never resumes decay, and the decay
 * base is baked in by SetDecayTime()). So the sustain-gate behavior from
 * CHORD-GEN.md is layered on top: the inner AdsrEnv runs with sustain level 0
 * (attack -> decay -> silence), and while the gate (FEED_1 / PATTERN G) is high
 * the output is held at 60% once the envelope has reached that level. On gate
 * release the hold ramps linearly to silence over the release time. The final
 * output is max(adsr, hold ramp), so a retrigger mid-release takes over
 * seamlessly and a high gate with no sounding voice stays silent.
 */
class SummonerVoiceEnv
{
public:
    /** @brief Sustain hold level while the gate is high (60% per CHORD-GEN.md). */
    static constexpr q31_t kSustainLevel = static_cast<q31_t>(Q31_MAX * 0.6f);

    /**
     * @brief Initializes the envelope.
     * @param sample_rate Audio sample rate in Hz.
     */
    void Init(const float sample_rate)
    {
        sample_rate_ = sample_rate;
        adsr_.Init(sample_rate);
        adsr_.SetSustainLevel(0); // pure AD; sustain is handled app-side
        adsr_.SetNonResetting(AdsrEnv::NonResetting::DECAY); // prevents clicks
        adsr_.SetAttackTime(0.002f);
        SetDecayTime(1.0f);
        ramp_ = 0;
        armed_ = false;
    }

    /**
     * @brief Sets the attack time (SHIFT+BANK+POT_4).
     * @param time Attack time in seconds.
     */
    void SetAttackTime(const float time)
    {
        adsr_.SetAttackTime(time);
    }

    /**
     * @brief Sets decay and release together (both live on PARAM_3 per the design).
     * @param time Decay/release time in seconds.
     */
    void SetDecayTime(const float time)
    {
        adsr_.SetDecayTime(time);
        float samples = time * sample_rate_;
        if (samples < 1.0f)
        {
            samples = 1.0f;
        }
        release_step_ = static_cast<q31_t>(static_cast<float>(kSustainLevel) / samples);
        if (release_step_ < 1)
        {
            release_step_ = 1;
        }
    }

    /**
     * @brief Fires the voice (starts the attack on the next Process call).
     */
    void Trigger()
    {
        adsr_.Trigger();
    }

    /**
     * @brief Silences the voice immediately and cancels any hold.
     */
    void Reset()
    {
        adsr_.Reset();
        ramp_ = 0;
        armed_ = false;
    }

    /**
     * @brief Processes one sample.
     * @param sustain_gate True while FEED_1 (PATTERN G) is high.
     * @return Envelope value 0..Q31_MAX.
     */
    q31_t Process(const bool sustain_gate)
    {
        const q31_t env = adsr_.Process();
        if (sustain_gate)
        {
            if (env >= kSustainLevel)
            {
                armed_ = true; // the voice got loud enough for the hold to grab it
            }
            if (armed_)
            {
                ramp_ = kSustainLevel;
            }
        }
        else
        {
            armed_ = false;
            if (ramp_ > 0)
            {
                ramp_ -= release_step_;
                if (ramp_ < 0)
                {
                    ramp_ = 0;
                }
            }
        }
        return (env > ramp_) ? env : ramp_;
    }

    /**
     * @brief Returns true while the voice is audible or attacking.
     */
    bool IsSounding() const
    {
        return adsr_.IsActive() || adsr_.GetOutput() > 0 || ramp_ > 0;
    }

private:
    AdsrEnv adsr_;
    float sample_rate_ = 0.0f;
    q31_t ramp_ = 0;         ///< App-side sustain hold / release ramp value
    q31_t release_step_ = 1; ///< Linear ramp decrement per sample on gate release
    bool armed_ = false;     ///< Hold engages only after the envelope reaches the sustain level
};

}
