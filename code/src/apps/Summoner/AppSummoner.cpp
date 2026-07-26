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

// Decay/length (POT_4 + attenuated LENGTH MOD CV): short pluck to long pad
constexpr auto kMapDecay = MapDef<float, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0.03f, 0.12f, 0.4f, 1.2f, 4.0f}};

// Strum speed (POT_3): frames between adjacent voices, 0 -> 300ms at 44kHz
// (per the CHORD-GEN.md strum table: 0 / ~8ms / ~30ms / ~80ms / ~300ms)
constexpr auto kMapStrum = MapDef<int32_t, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0, 352, 1320, 3520, SummonerStrum::kMaxStrumFrames}};

// A BANK press only cycles FX B when released within this time (and with no
// knob turn) — same gesture timing as the stock apps' bank/mode buttons
constexpr uint32_t kModeShortPressUnder = s2alr(1.5f);

// Attack time (SHIFT+BANK+POT_4): struck feel to slow pad swell
constexpr auto kMapAttack = MapDef<float, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0.002f, 0.02f, 0.1f, 0.4f, 1.0f}};

// Detune spread (SHIFT+BANK+POT_1): max per-voice offset in cents — modest so
// it reads as thickness, not out-of-tune (CHORD-GEN.md Voices)
constexpr float kMaxDetuneCents = 12.0f;

// Waveform select zones (SHIFT+BANK+POT_2); RAMP is excluded — same waveshape
// as SAW, just phase-mirrored (see CHORD-GEN.md Voices, 2026-07-17 note)
constexpr std::array<Oscillator::Waveform, 4> kWaveformZones = {
    Oscillator::Waveform::SINE,
    Oscillator::Waveform::TRI,
    Oscillator::Waveform::SAW,
    Oscillator::Waveform::SQUARE,
};

// Raw slot value (0-4095) whose zone is SAW — the power-on default waveform
constexpr int32_t kWaveformDefaultSlotValue = 2560;

// SHIFT+BANK fourth-layer slot -> physical pot (index = ComboSlot)
constexpr std::array<Hardware::Pot, SummonerComboLayer::kNumSlots> kComboSlotPots = {
    Hardware::Pot::POT_1,
    Hardware::Pot::POT_2,
    Hardware::Pot::POT_3,
    Hardware::Pot::POT_4,
    Hardware::Pot::POT_5,
    Hardware::Pot::POT_6,
    Hardware::Pot::POT_7,
};

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

    // Normal layer
    pots_[Pot::VOLUME] = FancyPot::Create({
        .pot = Hardware::Pot::POT_5,
        .layer = Hardware::Layer::NORMAL,
    });

    pots_[Pot::PITCH_OFFSET] = FancyPot::Create({
        .pot = Hardware::Pot::POT_1,
        .layer = Hardware::Layer::NORMAL,
        .deadzone = true,
    });

    pots_[Pot::DECAY] = FancyPot::Create({
        .pot = Hardware::Pot::POT_4,
        .layer = Hardware::Layer::NORMAL,
    });

    pots_[Pot::VOICING] = FancyPot::Create({
        .pot = Hardware::Pot::POT_2,
        .layer = Hardware::Layer::NORMAL,
        .initial_value = POT_MIN, // close voicing on power-up
    });

    pots_[Pot::STRUM_SPEED] = FancyPot::Create({
        .pot = Hardware::Pot::POT_3,
        .layer = Hardware::Layer::NORMAL,
        .initial_value = POT_MIN, // block chords on power-up
    });

    pots_[Pot::QUALITY] = FancyPot::Create({
        .pot = Hardware::Pot::POT_6,
        .layer = Hardware::Layer::NORMAL,
        .initial_value = POT_MIN, // major on power-up
    });

    // Shift layer
    pots_[Pot::PORTAMENTO] = FancyPot::Create({
        .pot = Hardware::Pot::POT_1,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MIN, // no glide until Phase 4 wires it
    });

    pots_[Pot::STRUM_DIR] = FancyPot::Create({
        .pot = Hardware::Pot::POT_3,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MIN, // low -> high default
        .map_size = static_cast<size_t>(SummonerStrum::Direction::COUNT),
        .memory_addr = kMemStrumDir,
    });

    pots_[Pot::LENGTH_ATTEN] = FancyPot::Create({
        .pot = Hardware::Pot::POT_4,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MAX, // LENGTH MOD CV fully active by default
    });

    pots_[Pot::CUTOFF] = FancyPot::Create({
        .pot = Hardware::Pot::POT_6,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MAX, // filter open; placeholder until Phase 6
    });

    // Mode (BANK) layer
    pots_[Pot::SCALE] = FancyPot::Create({
        .pot = Hardware::Pot::POT_1,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_HALF, // middle of the scale table = chromatic
        .map_size = quantizer_.GetScaleTableSize(),
        .memory_addr = kMemScale,
    });

    for (auto &pot : pots_)
    {
        pot->Init(AUDIO_LOOP_RATE);
    }

    combo_.Init();
    combo_.SetSlotValue(SlotIndex(ComboSlot::WAVEFORM), kWaveformDefaultSlotValue);
    uint8_t waveform_mem = 0;
    if (Kastle2::memory.Read8(kMemWaveform, &waveform_mem))
    {
        combo_.SetSlotValue(SlotIndex(ComboSlot::WAVEFORM), mem_to_pot(waveform_mem));
    }
    ApplyComboSlots(true);

    // FX B cycle: BANK press-release with no turn; a MODE-layer pot move or a
    // long hold cancels the pending change (stock bank-button coexistence rule)
    fx_mode_.Init();
    fx_mode_.DisableNextChangeWhen(pots_, kModeShortPressUnder);

    inited_ = true;
}

void AppSummoner::DeInit()
{
}

void AppSummoner::MemoryInitialization()
{
    Kastle2::memory.Write8(kMemScale, pot_to_mem(POT_HALF));   // chromatic
    Kastle2::memory.Write8(kMemStrumDir, pot_to_mem(POT_MIN)); // low -> high
    Kastle2::memory.Write8(kMemFxMode, std::to_underlying(FxB::OFF));
    Kastle2::memory.Write8(kMemWaveform, pot_to_mem(kWaveformDefaultSlotValue)); // saw
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

    for (auto &pot : pots_)
    {
        pot->Process();
    }
    fx_mode_.Process();
}

void AppSummoner::FireChord()
{
    // Root: FREE NOTE 1V/oct, sampled at fire time (stock convention for PITCH_2),
    // transposed by the POT_1 offset (+-1 octave, center = no transpose)
    const int32_t note_cv = Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PITCH_2);
    const float offset = static_cast<float>(pots_[Pot::PITCH_OFFSET]->GetValue() - pot(0.5f)) / static_cast<float>(pot(0.5f));
    float root = cv_to_freq_raw(kRootBase, note_cv);
    root *= std::pow(2.0f, offset * kPitchOffsetOctaves);

    // Quality: POT_6 zones (0/15/30/45/60/75/85/100% per the design table)
    const auto quality = SummonerChords::QualityFromQ15(pot_to_q15(pots_[Pot::QUALITY]->GetValue()));

    // Voicing: POT_2 continuous interpolation close -> open -> extended
    const float voicing = static_cast<float>(pots_[Pot::VOICING]->GetValue()) / static_cast<float>(POT_MAX);

    std::array<float, kNumVoices> frequencies;
    SummonerChords::ComputeChord(root, quality, voicing, quantizer_, frequencies);

    for (size_t v = 0; v < kNumVoices; v++)
    {
        oscs_[v].SetFrequency(fmin(frequencies[v] * detune_mult_[v], kMaxPitchHz));
    }

    // Strum: speed from POT_3, direction from SHIFT+POT_3
    const int32_t strum_frames = curve_map(pots_[Pot::STRUM_SPEED]->GetValue(), kMapStrum, MapClamp::TRUE);
    int32_t dir_index = pots_[Pot::STRUM_DIR]->GetMappedValue();
    if (dir_index < 0)
    {
        dir_index = 0;
    }
    strum_.Fire(strum_frames, static_cast<SummonerStrum::Direction>(dir_index), humanize_);
}

void AppSummoner::ProcessComboLayer()
{
    const bool combo_pressed = Kastle2::hw.Pressed(Hardware::Button::SHIFT) &&
                               Kastle2::hw.Pressed(Hardware::Button::MODE);

    std::array<int32_t, SummonerComboLayer::kNumSlots> raw;
    for (size_t s = 0; s < SummonerComboLayer::kNumSlots; s++)
    {
        raw[s] = Kastle2::hw.GetRawPotValue(kComboSlotPots[s]);
    }
    combo_.Process(combo_pressed, raw);

    if (combo_.IsActive() && combo_.AnyEngaged())
    {
        // Pot movement while both buttons are held cancels the hold's
        // press-actions and timers (CHORD-GEN.md button gestures): no tap
        // tempo, no Advanced Settings entry, no memory reset
        Kastle2::base.GetClock().ClearTaps();
        Kastle2::base.RestartShiftModeHoldTimer();
    }

    if (combo_.JustEnded() && combo_.AnyEngaged())
    {
        // Latch the underlying SHIFT/MODE-layer pots so the combo movement
        // doesn't leak into them once one button is released
        Kastle2::hw.FreezePots();
    }
}

void AppSummoner::ApplyComboSlots(const bool force)
{
    // Waveform select: 4 zones over the pot travel
    int32_t zone = (combo_.GetValue(SlotIndex(ComboSlot::WAVEFORM)) * static_cast<int32_t>(kWaveformZones.size())) / (POT_MAX + 1);
    if (zone < 0)
    {
        zone = 0;
    }
    if (zone >= static_cast<int32_t>(kWaveformZones.size()))
    {
        zone = static_cast<int32_t>(kWaveformZones.size()) - 1;
    }
    if (force || zone != waveform_zone_)
    {
        waveform_zone_ = zone;
        for (size_t v = 0; v < kNumVoices; v++)
        {
            oscs_[v].SetWaveform(kWaveformZones[zone]);
        }
        if (!force)
        {
            // Persist on zone change (not per pot tick — spares the EEPROM queue)
            Kastle2::memory.QueueUpdate8(kMemWaveform, pot_to_mem(combo_.GetValue(SlotIndex(ComboSlot::WAVEFORM))));
        }
    }

    // Detune spread: non-root voices at +d / -d / +2d cents; the root voice
    // stays true so CV_OUT 1V/oct tracking is unaffected
    if (force || combo_.HasChanged(SlotIndex(ComboSlot::DETUNE)))
    {
        const float d = kMaxDetuneCents *
                        static_cast<float>(combo_.GetValue(SlotIndex(ComboSlot::DETUNE))) /
                        static_cast<float>(POT_MAX);
        detune_mult_[0] = 1.0f;
        detune_mult_[1] = std::exp2(d / 1200.0f);
        detune_mult_[2] = std::exp2(-d / 1200.0f);
        detune_mult_[3] = std::exp2(2.0f * d / 1200.0f);
    }

    // Strum humanize: applied at the next chord fire
    humanize_ = static_cast<float>(combo_.GetValue(SlotIndex(ComboSlot::HUMANIZE))) /
                static_cast<float>(POT_MAX);

    // Attack time
    if (force || combo_.HasChanged(SlotIndex(ComboSlot::ATTACK)))
    {
        const float attack_time = curve_map(combo_.GetValue(SlotIndex(ComboSlot::ATTACK)), kMapAttack, MapClamp::TRUE);
        for (size_t v = 0; v < kNumVoices; v++)
        {
            envs_[v].SetAttackTime(attack_time);
        }
    }
}

void AppSummoner::UiLoop()
{
    if (do_fire_)
    {
        FireChord();
        do_fire_ = false;
    }

    ProcessComboLayer();
    ApplyComboSlots(false);

    // While the combo is held the physical pots belong to the fourth layer —
    // pausing ReadValue() keeps the SHIFT/MODE FancyPots from picking them up.
    // FancyMode is paused too: its MODE-layer SHIFT-press branch (stock
    // "previous bank") and its release-cycle must not fire on combo gestures.
    if (combo_.IsActive())
    {
        fx_mode_.DisableNextChange();
    }
    else
    {
        for (auto &pot : pots_)
        {
            pot->ReadValue();
        }
        fx_mode_.ReadValue();
        fx_b_ = static_cast<FxB>(fx_mode_.GetMode());
    }

    volume_ = pot_to_q15(pots_[Pot::VOLUME]->GetValue());

    // Quantizer scale: BANK+POT_1 stepped over the default scale table
    const int32_t scale_index = pots_[Pot::SCALE]->GetMappedValue();
    quantizer_.SetScale(scale_index >= 0 ? scale_index : 0);

    // Decay/length: POT_4 summed with LENGTH MOD CV (PARAM_3) attenuated by
    // SHIFT+POT_4, same time on all voices
    const int32_t decay_cv = apply_pot_mod(Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_3),
                                           pots_[Pot::LENGTH_ATTEN]->GetValue());
    const int32_t decay_val = pots_[Pot::DECAY]->GetValue() + decay_cv;
    const float decay_time = curve_map(decay_val, kMapDecay, MapClamp::TRUE);
    for (size_t v = 0; v < kNumVoices; v++)
    {
        envs_[v].SetDecayTime(decay_time);
    }

    // Sustain gate: chord holds at 60% while PATTERN G is high (analog tri-state,
    // ~9ms worst-case latency — fine for a sustain toggle)
    sustain_gate_ = (Kastle2::hw.GetFeedValue(Hardware::AnalogInput::FEED_1) == Hardware::FeedValue::HIGH);

    Kastle2::hw.SetLed(Hardware::Led::LED_1, fx_colors_[fx_b_]);
    // LED_2 white while the SHIFT+BANK fourth layer is held (proper LED design is Phase 9)
    Kastle2::hw.SetLed(Hardware::Led::LED_2, combo_.IsActive() ? WS2812::WHITE : WS2812::BLUE);
}
