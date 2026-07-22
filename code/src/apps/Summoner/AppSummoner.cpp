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

// Pitch offset (POT_1) range: +-1 octave around center
constexpr float kPitchOffsetOctaves = 1.0f;

// Decay/length (POT_4 + LENGTH MOD CV): short pluck to long pad
constexpr auto kMapDecay = MapDef<float, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0.03f, 0.12f, 0.4f, 1.2f, 4.0f}};

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

    pitch_pot_ = FancyPot::Create({
        .pot = Hardware::Pot::POT_1,
        .layer = Hardware::Layer::NORMAL,
        .deadzone = true,
    });
    pitch_pot_->Init(AUDIO_LOOP_RATE);

    decay_pot_ = FancyPot::Create({
        .pot = Hardware::Pot::POT_4,
        .layer = Hardware::Layer::NORMAL,
    });
    decay_pot_->Init(AUDIO_LOOP_RATE);

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

    // Fire path: every Base clock tick fires the full chord (euclidean sequencer
    // arrives in Phase 4); a TRIG_IN rising edge fires additively on top
    if (Kastle2::base.GetClock().IsNowTrigger())
    {
        do_fire_ = true;
    }
    if (trigger_detect_.Process(Kastle2::hw.GetTriggerIn()))
    {
        do_fire_ = true;
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
    pitch_pot_->Process();
    decay_pot_->Process();
}

void AppSummoner::FireChord()
{
    // Root: FREE NOTE 1V/oct, sampled at fire time (stock convention for PITCH_2),
    // transposed by the POT_1 offset (+-1 octave, center = no transpose)
    const int32_t note_cv = Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PITCH_2);
    const float offset = static_cast<float>(pitch_pot_->GetValue() - pot(0.5f)) / static_cast<float>(pot(0.5f));
    float root = cv_to_freq_raw(kRootBase, note_cv);
    root *= std::pow(2.0f, offset * kPitchOffsetOctaves);

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
    pitch_pot_->ReadValue();
    decay_pot_->ReadValue();
    volume_ = pot_to_q15(volume_pot_->GetValue());

    // Decay/length: POT_4 summed with LENGTH MOD CV (PARAM_3), same time on all voices
    const int32_t decay_val = decay_pot_->GetValue()
                              + Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_3);
    const float decay_time = curve_map(decay_val, kMapDecay, MapClamp::TRUE);
    for (size_t v = 0; v < kNumVoices; v++)
    {
        envs_[v].SetDecayTime(decay_time);
    }

    Kastle2::hw.SetLed(Hardware::Led::LED_1, WS2812::GREEN);
    Kastle2::hw.SetLed(Hardware::Led::LED_2, WS2812::BLUE);
}
