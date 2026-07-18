#include "AppSummoner.hpp"
#include "common/core/Kastle2.hpp"
#include "common/utils.hpp"

using namespace kastle2;

void AppSummoner::Init()
{
    inited_ = false;

    // App owns ENV/CV/GATE outputs; Base keeps LFO, SYNC, clock and audio chain
    Kastle2::base.SetFeatureEnabled(Base::Feature::ENV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::CV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::GATE_OUT, false);

    drone_osc_.Init(SAMPLE_RATE);
    drone_osc_.SetWaveform(Oscillator::Waveform::SAW);
    drone_osc_.SetFrequency(110.0f);

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
        q15_t sample = q15_mult(drone_osc_.Process(), volume_);

        output[2 * i] = sample;
        output[2 * i + 1] = sample;
    }

    volume_pot_->Process();
}

void AppSummoner::UiLoop()
{
    volume_pot_->ReadValue();

    // POT range 0..4095 -> q15 0..32760
    volume_ = static_cast<q15_t>(volume_pot_->GetValue() * 8);

    Kastle2::hw.SetLed(Hardware::Led::LED_1, WS2812::GREEN);
    Kastle2::hw.SetLed(Hardware::Led::LED_2, WS2812::BLUE);
}
