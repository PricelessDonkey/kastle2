#include "AppSummoner.hpp"
#include <cmath>
#include <cstdlib>
#include "common/core/Kastle2.hpp"
#include "common/dsp/synthesis/NoiseBlend.hpp"
#include "common/utils.hpp"
#include "SummonerTimbre.hpp"

using namespace kastle2;

namespace
{

// Root pitch with 0V CV and centered pitch offset: C3
constexpr float kRootBase = 130.8128f;

// Chord frequency ceiling (after offsets and voicing)
constexpr float kMaxPitchHz = 8000.0f;

// Strum seed — deterministic humanize jitter stream (any nonzero constant)
constexpr uint32_t kStrumSeed = 0x50111011;

// Groove seed — separate stream for the euclidean-hit skip rolls
constexpr uint32_t kGrooveSeed = 0x6400FE11;

// LFO shape seed — wander / sample-&-hold redraws at POT_7's extremes
constexpr uint32_t kLfoShapeSeed = 0x1F0BEA71;

// Pitch offset (POT_1) range: +-1 octave around center
constexpr float kPitchOffsetOctaves = 1.0f;

// Decay/length (POT_4 + attenuated LENGTH MOD CV): short pluck to long pad
constexpr auto kMapDecay = MapDef<float, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0.03f, 0.12f, 0.4f, 1.2f, 4.0f}};

// Strum speed (SHIFT+POT_3, knob-only since the 2026-08-12 direction/speed
// swap): frames between adjacent voices, 0 -> 300ms at 44kHz
// (per the CHORD-GEN.md strum table: 0 / ~8ms / ~30ms / ~80ms / ~300ms)
constexpr auto kMapStrum = MapDef<int32_t, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0, 352, 1320, 3520, SummonerStrum::kMaxStrumFrames}};

// Strum direction (POT_3 + PARAM_2 CV) power-on/memory default: middle of the
// Up zone — clean of the 0-3% broken-chord fray at the hard stop
constexpr int32_t kDirDefaultValue = pot(0.08f);

// A BANK press only cycles FX B when released within this time (and with no
// knob turn) — same gesture timing as the stock apps' bank/mode buttons
constexpr uint32_t kModeShortPressUnder = s2alr(1.5f);

// Portamento time (SHIFT+POT_1): off at zero, up to a slow 2s glide
constexpr auto kMapPortamento = MapDef<float, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {0.0f, 0.05f, 0.2f, 0.6f, 2.0f}};

// PARAM_1 movement that releases the PATTERN R voicing snap (~2% of range,
// safely above unconnected-jack ADC noise)
constexpr int32_t kVoicingSnapCvRelease = pot(0.02f);

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

// Fixed SoftClipper drive — "warmth before filter" (CHORD-GEN.md effects
// chain), no control mapped. Sits in WaveBard's warm range (its distortion
// map reaches q15(0.04) at 85% of the FX pot); compensation keeps level flat
constexpr q15_t kClipperDrive = q15(0.02f);

// Filter cutoff (SHIFT+POT_6): log sweep, ceiling well under Svf's rate/3 limit
constexpr auto kMapCutoff = MapDef<float, 5>{
    {pot(0.0f), pot(0.25f), pot(0.5f), pot(0.75f), pot(1.0f)},
    {50.0f, 200.0f, 800.0f, 3200.0f, SummonerTimbre::kMaxCutoffHz}};

// Filter resonance (BANK+POT_6): same span as ExampleSynth's kMapResonance —
// 0.1 floor (Svf warns below 0.005), 0.95 max stays stable
constexpr auto kMapResonance = MapDef<float, 3>{
    {pot(0.0f), pot(0.5f), pot(1.0f)},
    {0.1f, 0.6f, 0.95f}};

// Per-voice noise seeds — distinct nonzero constants so the four blend
// streams are uncorrelated (golden-ratio stride; values are arbitrary)
constexpr std::array<uint32_t, 4> kNoiseSeeds = {
    0x600D5EED,
    0x600D5EED + 0x9E3779B9,
    0x600D5EED + 2 * 0x9E3779B9,
    0x600D5EED + 3 * 0x9E3779B9,
};

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

    // App owns ENV/CV/GATE outputs; Base keeps SYNC, clock and audio chain.
    // LFO_OUT is app-owned too (added 2026-08-07): Base's LFO still runs, but
    // the app writes the TRI jack itself, scaled by BANK+POT_7 — PULSE is
    // passed through unchanged (LFO_OUT covers both jacks in Base)
    Kastle2::base.SetFeatureEnabled(Base::Feature::ENV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::CV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::GATE_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::LFO_OUT, false);

    for (size_t v = 0; v < kNumVoices; v++)
    {
        oscs_[v].Init(SAMPLE_RATE);
        oscs_[v].SetWaveform(Oscillator::Waveform::SAW);
        oscs_[v].SetFrequency(kRootBase);
        envs_[v].Init(SAMPLE_RATE);
        noises_[v].Seed(kNoiseSeeds[v]);
    }

    clipper_.Init(SAMPLE_RATE);
    clipper_.SetDrive(kClipperDrive);

    filter_.Init(SAMPLE_RATE);
    filter_.SetType(Svf::Type::LOWPASS);
    filter_.SetFrequency(SummonerTimbre::kMaxCutoffHz); // open until UiLoop takes over
    filter_.SetResonance(0.1f);

    strum_.Init(kStrumSeed);
    groove_.Init(kGrooveSeed);
    lfo_noise_.Seed(kLfoShapeSeed);
    lfo_shape_.Reset();
    voice_freq_.fill(kRootBase);

    euclid_.Init(); // power-on default: hits = length = 16, a chord on every tick

    portamento_.Init(AUDIO_LOOP_RATE);
    root_pitch_target_ = std::log2(kRootBase);

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

    pots_[Pot::STRUM_DIR] = FancyPot::Create({
        .pot = Hardware::Pot::POT_3,
        .layer = Hardware::Layer::NORMAL,
        .initial_value = kDirDefaultValue, // Up zone (memory overrides if set)
        .memory_addr = kMemStrumDir,
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

    pots_[Pot::STRUM_SPEED] = FancyPot::Create({
        .pot = Hardware::Pot::POT_3,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MIN, // block chords on power-up
    });

    pots_[Pot::LENGTH_ATTEN] = FancyPot::Create({
        .pot = Hardware::Pot::POT_4,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MAX, // LENGTH MOD CV fully active by default
    });

    pots_[Pot::CUTOFF] = FancyPot::Create({
        .pot = Hardware::Pot::POT_6,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MAX, // filter open on power-up
    });

    // Mode (BANK) layer
    pots_[Pot::SCALE] = FancyPot::Create({
        .pot = Hardware::Pot::POT_1,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_HALF, // middle of the scale table = chromatic
        .map_size = quantizer_.GetScaleTableSize(),
        .memory_addr = kMemScale,
    });

    pots_[Pot::DENSITY] = FancyPot::Create({
        .pot = Hardware::Pot::POT_3,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_MAX, // power-on default = K: a chord on every tick
    });

    pots_[Pot::LENGTH] = FancyPot::Create({
        .pot = Hardware::Pot::POT_4,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_MAX, // power-on default: 16 steps
        .map_size = SummonerSequencer::kLengthSteps,
    });

    pots_[Pot::FILTER_ENV] = FancyPot::Create({
        .pot = Hardware::Pot::POT_2,
        .layer = Hardware::Layer::MODE,
        .deadzone = true, // bipolar env->cutoff amount, center = off
    });

    pots_[Pot::RESONANCE] = FancyPot::Create({
        .pot = Hardware::Pot::POT_6,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_MIN, // no resonance on power-up
    });

    pots_[Pot::LFO_AMOUNT] = FancyPot::Create({
        .pot = Hardware::Pot::POT_7,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_MAX, // full positive = stock TRI behavior
        .deadzone = true,         // exact center = LFO off at the jack
    });

    for (auto &pot : pots_)
    {
        pot->Init(AUDIO_LOOP_RATE);
    }

    portamento_.SetSpeed(curve_map(pots_[Pot::PORTAMENTO]->GetValue(), kMapPortamento, MapClamp::TRUE));

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
    Kastle2::memory.Write8(kMemStrumDir, pot_to_mem(kDirDefaultValue)); // Up zone
    Kastle2::memory.Write8(kMemFxMode, std::to_underlying(FxB::OFF));
    Kastle2::memory.Write8(kMemWaveform, pot_to_mem(kWaveformDefaultSlotValue)); // saw
}

FASTCODE void AppSummoner::AudioLoop([[maybe_unused]] q15_t *input, q15_t *output, size_t size)
{
    if (!inited_)
    {
        return;
    }

    // Fire path: Base clock ticks step the euclidean pattern (in UiLoop — its
    // hits fire chords); a TRIG_IN rising edge fires additively on top
    if (Kastle2::base.GetClock().IsNowTrigger())
    {
        clock_tick_ = true;
    }
    if (trigger_detect_.Process(Kastle2::hw.GetTriggerIn()))
    {
        do_fire_ = true;
    }

    portamento_.TimeTick();

    for (size_t i = 0; i < size; i++)
    {
        // Groove counts frames for its step-period measurement; a true return
        // is a swung euclidean hit maturing (fired via UiLoop like TRIG_IN)
        if (groove_.Tick())
        {
            groove_fire_ = true;
        }
        const uint32_t fired = strum_.Tick();
        int32_t mix = 0;
        int32_t env_sum = 0;

        for (size_t v = 0; v < kNumVoices; v++)
        {
            if (fired & (1u << v))
            {
                envs_[v].Trigger();
            }
            const q15_t env = q31_to_q15(envs_[v].Process(sustain_gate_));
            env_sum += env;
            // Equal-power noise blend, pre-envelope and pre-filter — the same
            // envelope and cutoff shape both tone and noise together
            const q15_t tone = q15_add(q15_mult(oscs_[v].Process(), noise_dry_gain_),
                                       q15_mult(noises_[v].Process(), noise_wet_gain_));
            mix += q15_mult(tone, env);
        }

        env_mix_ = static_cast<q15_t>(env_sum / static_cast<int32_t>(kNumVoices));

        // Core 0 effects chain: mix -> SoftClipper (warmth) -> Svf LP -> volume
        q15_t sample = static_cast<q15_t>(mix / static_cast<int32_t>(kNumVoices));
        sample = filter_.Process(clipper_.Process(sample));
        sample = q15_mult(sample, volume_);

        output[2 * i] = sample;
        output[2 * i + 1] = sample;
    }

    // LFO TRI jack, app-scaled (Base's LFO_OUT is disabled): amplitude grows
    // from the 0V floor so low amounts stay useful as pitch CV — no constant
    // offset at the midpoint. Negative amounts flip the triangle; PULSE is
    // passed through exactly as Base would write it (always the clean square).
    //
    // Before scaling, POT_7's outer 20% zones reshape the triangle into
    // wander / sample-&-hold (redrawn once per LFO cycle at the phase wrap).
    // IsLastSample() is true on the buffer before the wrap; its rising edge
    // marks the redraw point.
    const bool lfo_last = Kastle2::base.GetLfo().IsLastSample();
    const bool lfo_wrapped = lfo_last && !lfo_last_sample_prev_;
    lfo_last_sample_prev_ = lfo_last;
    const int32_t tri = lfo_shape_.Process(
        static_cast<int32_t>(Kastle2::base.GetLfoTriangle()),
        lfo_wrapped, lfo_rate_pot_, lfo_noise_);
    const int32_t tri_scaled = (lfo_amount_ >= 0)
                                   ? (tri * lfo_amount_) / POT_HALF
                                   : ((DAC_MAX - tri) * -lfo_amount_) / POT_HALF;
    Kastle2::hw.SetTriOut(tri_scaled);
    Kastle2::hw.SetPulseOut(Kastle2::base.GetLfo().GetSquareOut());

    for (auto &pot : pots_)
    {
        pot->Process();
    }
    fx_mode_.Process();
}

void AppSummoner::FireChord()
{
    // Root: NOTE input 1V/oct, sampled at fire time (stock convention for
    // PITCH_2), transposed by the POT_1 offset (+-1 octave, center = no
    // transpose). Jump intensity is tamed at the source instead: BANK+POT_7
    // scales the LFO TRI jack (reverted 2026-08-07 from a NOTE-CV attenuator
    // so external sequencers always track true 1V/oct)
    const int32_t note_cv = Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PITCH_2);
    const float offset = static_cast<float>(pots_[Pot::PITCH_OFFSET]->GetValue() - pot(0.5f)) / static_cast<float>(pot(0.5f));
    float root = cv_to_freq_raw(kRootBase, note_cv);
    root *= std::pow(2.0f, offset * kPitchOffsetOctaves);

    // Portamento glides toward the new root: chord tones are computed from the
    // target and scaled by the glide ratio each UiLoop pass
    root_pitch_target_ = std::log2(root);

    // Quality: POT_6 zones (0/15/30/45/60/75/85/100% per the design table)
    // summed with the BANK/MODE CV — stepped zone selection like stock bank select
    const int32_t quality_val = pots_[Pot::QUALITY]->GetValue() +
                                Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::MODE);
    const auto quality = SummonerChords::QualityFromQ15(pot_to_q15(quality_val));

    // Voicing: POT_2 summed with SAMPLE MOD CV (PARAM_1), continuous
    // close -> open -> extended; the PATTERN R snap forces 0% until movement
    const int32_t voicing_val = constrain(pots_[Pot::VOICING]->GetValue() +
                                              Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_1),
                                          POT_MIN, POT_MAX);
    const float voicing = voicing_snap_ ? 0.0f : static_cast<float>(voicing_val) / static_cast<float>(POT_MAX);

    SummonerChords::ComputeChord(root, quality, voicing, quantizer_, voice_freq_);

    // Strum: direction from POT_3 summed with LFO MOD CV (PARAM_2) — 6 zones,
    // with the broken-chord skip fraying in at the range's two hard ends;
    // speed from SHIFT+POT_3, knob-only (2026-08-12 swap)
    const int32_t strum_frames = curve_map(pots_[Pot::STRUM_SPEED]->GetValue(), kMapStrum, MapClamp::TRUE);
    const q15_t dir_val = pot_to_q15(pots_[Pot::STRUM_DIR]->GetValue() +
                                     Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_2));
    strum_.Fire(strum_frames, SummonerStrum::DirectionFromQ15(dir_val), humanize_,
                SummonerStrum::SkipChanceFromQ15(dir_val));
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

    // Noise blend: equal-power gains recomputed only on slot change so the
    // trig calls stay out of the audio path
    if (force || combo_.HasChanged(SlotIndex(ComboSlot::NOISE_BLEND)))
    {
        const q15_t blend = pot_to_q15(combo_.GetValue(SlotIndex(ComboSlot::NOISE_BLEND)));
        noise_dry_gain_ = NoiseBlendGainDry(blend);
        noise_wet_gain_ = NoiseBlendGainWet(blend);
    }

    // Groove: humanize (bottom zone) feeds the strum scheduler at the next
    // chord fire; swing/skip read groove_q15_ in the euclidean-hit path
    groove_q15_ = pot_to_q15(combo_.GetValue(SlotIndex(ComboSlot::GROOVE)));
    humanize_ = SummonerGroove::HumanizeFromQ15(groove_q15_);

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
    // PATTERN R (FEED_2) rising edge: generator reset — pattern to step 1,
    // pending strum cancelled, voicing snapped to 0% until knob/CV movement
    const bool feed2 = (Kastle2::hw.GetFeedValue(Hardware::AnalogInput::FEED_2) == Hardware::FeedValue::HIGH);
    if (feed2 && !feed2_high_)
    {
        euclid_.Reset();
        strum_.Reset();
        groove_.Reset();
        voicing_snap_ = true;
        voicing_cv_at_snap_ = Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_1);
    }
    feed2_high_ = feed2;

    // Sequencer params: density = BANK+POT_3 + PATTERN C (FEED_3) CV,
    // cycle length = BANK+POT_4. Density 0 = silent (the knob normal-breaker).
    const size_t length = SummonerSequencer::LengthFromMapped(pots_[Pot::LENGTH]->GetMappedValue());
    const int32_t density = pots_[Pot::DENSITY]->GetValue() +
                            SummonerSequencer::DensityCvToPot(Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::FEED_3));
    euclid_.SetPattern(SummonerSequencer::DensityToHits(density, length), length);

    // Clock ticks step the pattern and its hits fire chords, gated/shifted by
    // Groove's swing and skip zones; TRIG_IN fires additively on top via
    // do_fire_ and never passes through Groove (density's normal-breaker scope)
    if (clock_tick_)
    {
        clock_tick_ = false;
        groove_.OnClockTick();
        if (euclid_.Step())
        {
            switch (groove_.ProcessHit(euclid_.GetStep(), groove_q15_))
            {
            case SummonerGroove::HitAction::FIRE:
                do_fire_ = true;
                break;
            case SummonerGroove::HitAction::DEFERRED:
            case SummonerGroove::HitAction::SKIPPED:
                break;
            }
        }
    }

    if (groove_fire_)
    {
        groove_fire_ = false;
        do_fire_ = true;
    }

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

    // PATTERN R voicing snap releases on voicing knob or SAMPLE MOD CV movement
    if (voicing_snap_)
    {
        const int32_t voicing_cv = Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_1);
        if (pots_[Pot::VOICING]->HasChanged() ||
            std::abs(voicing_cv - voicing_cv_at_snap_) > kVoicingSnapCvRelease)
        {
            voicing_snap_ = false;
        }
    }

    // Portamento: glide the root in pitch space and scale all chord tones by
    // the glide ratio (1.0 once the glide lands); detune spread on top
    if (pots_[Pot::PORTAMENTO]->HasChanged())
    {
        portamento_.SetSpeed(curve_map(pots_[Pot::PORTAMENTO]->GetValue(), kMapPortamento, MapClamp::TRUE));
    }
    const float glided_pitch = portamento_.Track(root_pitch_target_);
    const float glide_ratio = std::exp2(glided_pitch - root_pitch_target_);
    for (size_t v = 0; v < kNumVoices; v++)
    {
        oscs_[v].SetFrequency(fmin(voice_freq_[v] * glide_ratio * detune_mult_[v], kMaxPitchHz));
    }

    // Root pitch 1V/oct out: the sounding root — quantized and glided, detune
    // never applies to voice 0 — with C3 (kRootBase, the 0V-CV root) = 0V out.
    // DAC_1V is USB-power calibration only (HARDWARE.md); roots below C3 clamp
    // to 0V inside SetCvOut.
    const float root_octaves = std::log2(voice_freq_[0] * glide_ratio / kRootBase);
    Kastle2::hw.SetCvOut(static_cast<int32_t>(root_octaves * static_cast<float>(DAC_1V) + 0.5f));

    volume_ = pot_to_q15(pots_[Pot::VOLUME]->GetValue());

    // LFO TRI amplitude/polarity (BANK+POT_7): the deadzone plateau makes
    // exact center a clean "LFO off at the jack"
    lfo_amount_ = pots_[Pot::LFO_AMOUNT]->GetValue() - POT_HALF;

    // LFO shape extremes: cache POT_7's rate-knob position for the AudioLoop
    // reshape, and while in an outer 20% zone pin the LFO rate at that zone's
    // boundary — turning further morphs the wave (wander / sample & hold)
    // instead of changing speed. Base's BeforeUiLoop already set the rate from
    // the true pot; we re-apply the clamped boundary value on top, mirroring
    // Base's own rate maps. (LFO MOD CV on rate is intentionally frozen in the
    // zones too — the "rate" concept is fixed there.) Skipped while the combo
    // layer owns the pots.
    if (!combo_.IsActive())
    {
        Kastle2::hw.ReadPot(&lfo_rate_pot_, Hardware::Pot::POT_7, Hardware::Layer::NORMAL);
        if (SummonerLfoShape::InExtremeZone(lfo_rate_pot_))
        {
            const int32_t pinned = SummonerLfoShape::EffectiveRatePot(lfo_rate_pot_);
            Lfo &lfo = Kastle2::base.GetLfo();
            if (pinned <= POT_HALF)
            {
                // Synced side (0–20%): pin to the boundary's clock ratio.
                const int32_t idx = constrain(curve_map(pinned, kBaseLfoRatioMap), 0,
                                              static_cast<int32_t>(kBaseLfoRatios.size()) - 1);
                lfo.SetClockTicks(Kastle2::base.GetClock().GetTargetTicks());
                lfo.SetRatio(kBaseLfoRatios[idx]);
            }
            else
            {
                // Free side (80–100%): pin to the boundary's frequency.
                lfo.SetFrequency(curve_map(pinned, kBaseLfoMap));
            }
        }
    }

    // Filter: cutoff (SHIFT+POT_6) modulated by the summed voice envelope per
    // the bipolar env amount (BANK+POT_2, center off) — right of center chord
    // hits open the filter, left of center they darken; resonance BANK+POT_6
    const float cutoff_base = curve_map(pots_[Pot::CUTOFF]->GetValue(), kMapCutoff, MapClamp::TRUE);
    const float env_amount = SummonerTimbre::EnvAmountFromPot(pots_[Pot::FILTER_ENV]->GetValue());
    filter_.SetFrequency(SummonerTimbre::ModulatedCutoffHz(cutoff_base, env_amount, env_mix_));
    filter_.SetResonance(curve_map(pots_[Pot::RESONANCE]->GetValue(), kMapResonance, MapClamp::TRUE));

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

    // Chord gate: high while any voice is sounding (attack/decay, sustain hold
    // or release ramp). SetGateOut handles GPIO 3's inverted logic internally.
    bool any_sounding = false;
    for (const auto &env : envs_)
    {
        any_sounding = any_sounding || env.IsSounding();
    }
    Kastle2::hw.SetGateOut(any_sounding);

    // Mix envelope out: q15 -> 10-bit PWM (attack pluck + decay tail as a real
    // modulation source; sustained full chord sits at ~60% of range)
    Kastle2::hw.SetEnvOut(static_cast<int32_t>(env_mix_) >> (15 - 10));

    Kastle2::hw.SetLed(Hardware::Led::LED_1, fx_colors_[fx_b_]);
    // LED_2 white while the SHIFT+BANK fourth layer is held (proper LED design is Phase 9)
    Kastle2::hw.SetLed(Hardware::Led::LED_2, combo_.IsActive() ? WS2812::WHITE : WS2812::BLUE);
}
