#include "AppSummoner.hpp"
#include <cmath>
#include "common/core/Kastle2.hpp"
#include "common/utils.hpp"

using namespace kastle2;

namespace
{

// Root pitch with 0V CV and centered pitch offset: C3
constexpr float kRootBase = 130.8128f;

// Chord frequency ceiling (after offsets and voicing)
constexpr float kMaxPitchHz = 8000.0f;

// Strum seed — deterministic humanize jitter stream (any nonzero constant)
constexpr uint32_t kStrumSeed = 0x50111011;

}

void AppSummoner::Init()
{
    inited_ = false;

    // App owns ENV/CV/GATE outputs; Base keeps LFO, SYNC, clock and audio chain
    Kastle2::base.SetFeatureEnabled(Base::Feature::ENV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::CV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::GATE_OUT, false);

    for (size_t v = 0; v < kNumVoices; v++)
    {
        oscs_[v].Init(SAMPLE_RATE);
        oscs_[v].SetWaveform(Oscillator::Waveform::SAW);
        oscs_[v].SetFrequency(kRootBase);
        envs_[v].Init(SAMPLE_RATE);
    }

    strum_.Init(kStrumSeed);

    quantizer_.Init(0.8f);
    quantizer_.SetEnabled(true);
    quantizer_.SetScale(Quantizer::DefaultScale::CHROMATIC);

    volume_pot_ = FancyPot::Create({
        .pot = Hardware::Pot::POT_5,
        .layer = Hardware::Layer::NORMAL,
    });
    volume_pot_->Init(AUDIO_LOOP_RATE);

    inited_ = true;
}

void AppSummoner::DeInit()
{
}

FASTCODE void AppSummoner::AudioLoop([[maybe_unused]] q15_t *input, q15_t *output, size_t size)
{
    if (!inited_)
    {
        return;
    }

    for (size_t i = 0; i < size; i++)
    {
        const uint32_t fired = strum_.Tick();
        int32_t mix = 0;

        for (size_t v = 0; v < kNumVoices; v++)
        {
            if (fired & (1u << v))
            {
                envs_[v].Trigger();
            }
            const q15_t osc_out = oscs_[v].Process();
            mix += q15_mult(osc_out, q31_to_q15(envs_[v].Process(sustain_gate_)));
        }

        const q15_t sample = q15_mult(static_cast<q15_t>(mix / static_cast<int32_t>(kNumVoices)), volume_);

        output[2 * i] = sample;
        output[2 * i + 1] = sample;
    }

    volume_pot_->Process();
}

void AppSummoner::FireChord()
{
    const float root = cv_to_freq_raw(kRootBase, 0);

    std::array<float, kNumVoices> frequencies;
    SummonerChords::ComputeChord(root, SummonerChords::Quality::MAJOR, 0.0f,
                                 quantizer_, frequencies);

    for (size_t v = 0; v < kNumVoices; v++)
    {
        oscs_[v].SetFrequency(fmin(frequencies[v], kMaxPitchHz));
    }

    strum_.Fire(0, SummonerStrum::Direction::LOW_TO_HIGH, 0.0f);
}

void AppSummoner::UiLoop()
{
    if (do_fire_)
    {
        FireChord();
        do_fire_ = false;
    }

    volume_pot_->ReadValue();
    volume_ = pot_to_q15(volume_pot_->GetValue());

    Kastle2::hw.SetLed(Hardware::Led::LED_1, WS2812::GREEN);
    Kastle2::hw.SetLed(Hardware::Led::LED_2, WS2812::BLUE);
}
