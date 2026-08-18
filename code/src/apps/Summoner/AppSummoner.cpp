#include "AppSummoner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "common/core/Kastle2.hpp"
#include "common/core/MultiCore.hpp"
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

// PARAM_1 movement that releases the PATTERN R voicing snap (~2% of range,
// safely above unconnected-jack ADC noise)
constexpr int32_t kVoicingSnapCvRelease = pot(0.02f);

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

// FX B delay time (SHIFT+BANK+POT_5 in DELAY / BOTH): ~11ms → ~500ms. The right
// channel runs shorter for a stereo spread that survives into the dry path.
constexpr auto kMapFxDelay = MapDef<int32_t, 3>{
    {pot(0.0f), pot(0.5f), pot(1.0f)},
    {500, 8000, 22000}};

// FX B crush sample rate (CRUSH): knob up = more crush (heavier downsampling),
// clean at the bottom down to BitCrusher's ~688Hz floor at the top.
constexpr auto kMapFxCrushRate = MapDef<int32_t, 3>{
    {pot(0.0f), pot(0.5f), pot(1.0f)},
    {BitCrusher::kMaxSampleRate, Q15_MAX / 8, BitCrusher::kMinSampleRate}};

// FX B crush bit depth (CRUSH): 12 bits (subtle) → 4 bits (gritty), same knob.
constexpr auto kMapFxCrushBits = MapDef<int32_t, 3>{
    {pot(0.0f), pot(0.5f), pot(1.0f)},
    {12, 8, 4}};

// FX B delay feedback — fixed; a few repeats without runaway self-oscillation.
constexpr q15_t kFxDelayFeedback = q15(0.4f);

// FX B power-on defaults for the two SHIFT+BANK slots. The combo layer defaults
// every slot to 0, which for FX B mix means "fully dry" — cycling to Delay/Crush
// would then be silent until the user discovers SHIFT+BANK+POT_7. Seed an
// audible mix and a musical mid-range parameter so a plain BANK cycle is heard
// immediately; the pickup logic keeps these until POT_5/POT_7 are actually moved
// in the fourth layer.
constexpr int32_t kFxMixDefault = pot(0.6f);   // ~60% wet — clearly audible
constexpr int32_t kFxParamDefault = pot(0.4f); // ~mid delay time / crush amount

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

    // Base's stock LFO-rate modulation reads POT_3 (primary) for depth and
    // PARAM_2 for the CV — both repurposed here (strum direction, knob summed
    // with the same jack per CHORD-GEN.md's CV table). Left enabled, setting a
    // strum direction also dialled in LFO-rate modulation, and the direction CV
    // wobbled the TRI/PULSE jacks (leak audit, 2026-08-18). The LFO rate stays
    // POT_7's alone; Base's LFO itself is still used (GetLfoTriangle/GetLfo).
    Kastle2::base.SetFeatureEnabled(Base::Feature::LFO_MOD, false);

    // Base's stock INPUT_GAIN reads SHIFT+POT_1, which this app repurposes for
    // the tremolo rate (portamento until 2026-08-16) — disable it either way,
    // it would scale the input path off a knob that means something else here.
    Kastle2::base.SetFeatureEnabled(Base::Feature::INPUT_GAIN, false);

    // OUTPUT_GAIN (stock main volume) is *kept enabled* as of 2026-08-14. It was
    // disabled in 7bfbd6f because the reverb blend then lived on SHIFT+POT_5 and
    // the stock gain silently scaled the buffer to 0 with it; the Phase 10 swap
    // put volume back on SHIFT+POT_5, so the collision is gone and Base's pot is
    // pointing at the right control again. The app's own digital volume (a plain
    // q15 multiply in SecondCoreProcess) is retired with it: Base's version also
    // drives the codec headphone volume (SetHpVolume) as well as the digital
    // gain, which is what actually tames the analog output level — a digital-only
    // volume left the output hot no matter where the knob sat. Bonus: the stock
    // control is EEPROM-persisted (ADDR_OUTPUT_GAIN) and MIDI-CC addressable.
    // Base::AfterAudioLoop applies it after AudioLoop returns, i.e. after Core 1
    // has finished the block (we WaitForMessage(DONE) before returning), so the
    // scaling lands on the finished stereo mix.

    // LED_1 is the app's FX B state indicator (fx_colors_). Base's stock input
    // loudness meter (INPUT_INDICATION, in BeforeUiLoop) and its red clip flash
    // (INPUT_INDICATION_CLIP, in AfterUiLoop — runs *after* the app's UiLoop and
    // would override our color) both write LED_1. Disable both so the FX B color
    // is the only thing on that LED (WaveBard disables the clip one for the same
    // reason). The app has no audio-input meter to show anyway.
    Kastle2::base.SetFeatureEnabled(Base::Feature::INPUT_INDICATION, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::INPUT_INDICATION_CLIP, false);

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

    // ShimmerReverb runs on Core 1 with the clipper/filter (Phase 7). Init sets
    // musical defaults (decay 0.85, shimmer 0, fifth-up); UiLoop drives them.
    reverb_.Init(SAMPLE_RATE);

    // FX B (Phase 8): heap-allocated delay line (capped at kFxDelayMax) + a
    // header-only crusher, both sit pre-reverb on Core 1. The delay runs fully
    // wet — the FX B mix knob crossfades dry↔effected externally so one knob
    // serves DELAY / CRUSH / BOTH uniformly. Crush defaults are moderate (used
    // as-is in BOTH, where the param knob drives the delay time instead).
    fx_delay_.Init(SAMPLE_RATE);
    fx_delay_.SetWet(Q15_MAX);
    fx_delay_.SetFeedback(kFxDelayFeedback);
    fx_crusher_.Init();

    strum_.Init(kStrumSeed);
    groove_.Init(kGrooveSeed);
    lfo_noise_.Seed(kLfoShapeSeed);
    lfo_shape_.Reset();
    voice_freq_.fill(kRootBase);

    euclid_.Init(); // power-on default: hits = length = 16, a chord on every tick

    tremolo_.Init(SAMPLE_RATE);

    quantizer_.Init(0.8f);
    quantizer_.SetEnabled(true);
    quantizer_.SetScale(Quantizer::DefaultScale::CHROMATIC);

    // Normal layer
    // POT_5 primary is the reverb combo (Phase 10 swap 2026-08-13): the dry/wet
    // + decay knob is the live-play control, on the front street. Volume moves to
    // SHIFT+POT_5 (set-and-forget). OUTPUT_GAIN stays disabled (see Init above),
    // so no stock gain scales the buffer with volume now physically on that pot.
    pots_[Pot::REVERB_BLEND] = FancyPot::Create({
        .pot = Hardware::Pot::POT_5,
        .layer = Hardware::Layer::NORMAL,
        .initial_value = POT_MIN, // fully dry on power-up (dry chord, no reverb)
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
    // No map_size: SummonerTremolo quantizes the knob itself (as
    // SummonerNoiseFold does), so the FancyPot stays continuous.
    pots_[Pot::TREMOLO] = FancyPot::Create({
        .pot = Hardware::Pot::POT_1,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MIN, // tremolo OFF on power-up
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

    // No app-owned volume pot: SHIFT+POT_5 is Base's stock main volume again
    // (Feature::OUTPUT_GAIN, re-enabled 2026-08-14 — see Init).

    pots_[Pot::INTERVAL] = FancyPot::Create({
        .pot = Hardware::Pot::POT_2,
        .layer = Hardware::Layer::SHIFT,
        .initial_value = POT_MIN, // fifth-up is the second zone; see UiLoop map
        .map_size = static_cast<uint32_t>(ShimmerReverb::Interval::COUNT),
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

    pots_[Pot::SHIMMER] = FancyPot::Create({
        .pot = Hardware::Pot::POT_5,
        .layer = Hardware::Layer::MODE,
        .initial_value = POT_MIN, // clean plate on power-up
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

    combo_.Init();
    combo_.SetSlotValue(SlotIndex(ComboSlot::WAVEFORM), kWaveformDefaultSlotValue);
    // FX B mix/param aren't EEPROM-persisted; seed audible defaults so a BANK
    // cycle to Delay/Crush is heard without first hunting for SHIFT+BANK+POT_7.
    combo_.SetSlotValue(SlotIndex(ComboSlot::FX_B_MIX), kFxMixDefault);
    combo_.SetSlotValue(SlotIndex(ComboSlot::FX_B_PARAM), kFxParamDefault);
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

    // Hand the block to Core 1 (WaveBard/FxWizard lock-step). Core 0 synthesises
    // each dry mono sample into the output buffer, then requests Core 1 to run
    // the effects chain (clipper → Svf → ShimmerReverb → volume) on it in place.
    output_buffer_ = output;
    buffer_size_ = size;
    MultiCore::SendMessage(MultiCore::MessageType::BEGIN);

    for (size_t i = 0; i < size; i++)
    {
        // Groove counts frames for its step-period measurement; a true return
        // is a swung euclidean hit maturing (fired via UiLoop like TRIG_IN)
        if (groove_.Tick())
        {
            groove_fire_ = true;
        }
        const uint32_t fired = strum_.Tick();
        // Tremolo gate: pre-FX and per-voice, so reverb/delay tails ring through
        // the gaps. env_sum stays un-gated below — it drives ENV_OUT and the
        // filter cutoff, and gating it would make the chop read as a filter sweep.
        const q15_t trem_gain = tremolo_.Tick();
        int32_t mix = 0;
        int32_t env_sum = 0;

        for (size_t v = 0; v < kNumVoices; v++)
        {
            if (fired & (1u << v))
            {
                envs_[v].Trigger();
                noise_env_[v] = 0; // restart the noise attack ramp (folded knob)
            }
            const q15_t env = q31_to_q15(envs_[v].Process(sustain_gate_));
            env_sum += env;
            // Noise gets its own attack ramp before the equal-power blend: instant
            // below the knob's 50% center (immediate chiff), swelling in above it
            // (SummonerNoiseFold). A huge inc collapses the ramp to instant.
            noise_env_[v] += noise_attack_inc_;
            if (noise_env_[v] > Q15_MAX)
            {
                noise_env_[v] = Q15_MAX;
            }
            const q15_t noise_atk = q15_mult(noises_[v].Process(), static_cast<q15_t>(noise_env_[v]));
            // Equal-power noise blend, pre-envelope and pre-filter — the same
            // envelope and cutoff shape both tone and noise together
            const q15_t tone = q15_add(q15_mult(oscs_[v].Process(), noise_dry_gain_),
                                       q15_mult(noise_atk, noise_wet_gain_));
            // Voice 0 (root/bass) is exempt across the knob's upper half — the
            // low end sustains while the upper voices are chopped.
            const q15_t vg = (trem_bass_exempt_ && v == 0) ? Q15_MAX : trem_gain;
            mix += q15_mult(q15_mult(tone, env), vg);
        }

        env_mix_ = static_cast<q15_t>(env_sum / static_cast<int32_t>(kNumVoices));

        // Write the dry mono mix; Core 1 reads it, applies the effects chain and
        // overwrites both channels with the stereo wet mix once requested.
        const q15_t sample = static_cast<q15_t>(mix / static_cast<int32_t>(kNumVoices));
        output[2 * i] = sample;
        output[2 * i + 1] = sample;
        MultiCore::SendMessage(MultiCore::MessageType::SAMPLE_REQUEST, i);
    }

    // Block the audio callback until Core 1 has processed every sample.
    MultiCore::WaitForMessage(MultiCore::MessageType::DONE);

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

FASTCODE void AppSummoner::SecondCoreProcess(size_t index)
{
    // Core 0 wrote the dry mono mix to both channels; read one channel.
    const q15_t dry = output_buffer_[2 * index];

    // Effects chain: SoftClipper (warmth) → Svf LP → FX B → ShimmerReverb.
    const q15_t voiced = filter_.Process(clipper_.Process(dry));

    // FX B slot (pre-reverb): OFF passes voiced straight through; CRUSH/DELAY/
    // BOTH process it, crossfaded dry↔effected by fx_mix_ (SHIFT+BANK+POT_7).
    // The result feeds both the dry path and the reverb input, so delay echoes
    // feed the shimmer tail (CHORD-GEN.md FX B). Delay runs stereo; its spread
    // survives into the dry L/R because the reverb only mixes a mono sum in.
    q15_t fx_l = voiced;
    q15_t fx_r = voiced;
    if (fx_b_ != FxB::OFF)
    {
        q15_t pre = voiced;
        if (fx_b_ == FxB::CRUSH || fx_b_ == FxB::BOTH)
        {
            pre = fx_crusher_.Process(voiced);
        }
        q15_t wet_l = pre;
        q15_t wet_r = pre;
        if (fx_b_ == FxB::DELAY || fx_b_ == FxB::BOTH)
        {
            const StereoDelay::Output d = fx_delay_.Process(pre, pre);
            wet_l = d.left;
            wet_r = d.right;
        }
        const q15_t fx_dry = static_cast<q15_t>(Q15_MAX - fx_mix_);
        fx_l = q15_add(q15_mult(voiced, fx_dry), q15_mult(wet_l, fx_mix_));
        fx_r = q15_add(q15_mult(voiced, fx_dry), q15_mult(wet_r, fx_mix_));
    }

    // Dry↔wet crossfade from SHIFT+POT_5 (reverb_wet_): at 0 the reverb is fully
    // bypassed (pure dry chord — a clean dry signal is always reachable), at
    // Q15_MAX it's the stereo wet tail only. Keep feeding the reverb every sample
    // regardless so the tail is continuous as the mix opens.
    const q15_t rev_in = static_cast<q15_t>((fx_l + fx_r) >> 1);
    const ShimmerReverb::Output wet = reverb_.Process(rev_in);
    const q15_t dry_gain = static_cast<q15_t>(Q15_MAX - reverb_wet_);
    // No app volume multiply here — Base's OUTPUT_GAIN scales the finished
    // buffer in AfterAudioLoop (2026-08-14).
    const q15_t left = q15_add(q15_mult(fx_l, dry_gain), q15_mult(wet.left, reverb_wet_));
    const q15_t right = q15_add(q15_mult(fx_r, dry_gain), q15_mult(wet.right, reverb_wet_));

    output_buffer_[2 * index] = left;
    output_buffer_[2 * index + 1] = right;
}

FASTCODE void AppSummoner::SecondCoreWorker()
{
    while (inited_)
    {
        if (MultiCore::HasMessage())
        {
            MultiCore::Message m = MultiCore::GetMessage();
            switch (m.type)
            {
            case MultiCore::MessageType::BEGIN:
                second_core_processed_samples_ = 0;
                break;
            case MultiCore::MessageType::SAMPLE_REQUEST:
                SecondCoreProcess(m.data);
                second_core_processed_samples_++;
                if (second_core_processed_samples_ == buffer_size_)
                {
                    MultiCore::SendMessage(MultiCore::MessageType::DONE);
                }
                break;
            case MultiCore::MessageType::DONE:
                break;
            }
        }
    }
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

    // The combo borrows the physical pots, but the hardware layer stays SHIFT,
    // so Base would otherwise keep tracking them with its own FancyPots — with
    // Feature::OUTPUT_GAIN enabled that made SHIFT+BANK+POT_5 (FX B param) move
    // the main volume (bug, Sam 2026-08-18). Pause Base's pot reads for the
    // duration of the hold, exactly as the app pauses its own below.
    Kastle2::base.SetPotsPaused(combo_.IsActive());

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

    // Noise blend (folded, Phase 10): the knob folds blend amount with a noise-
    // specific attack (SummonerNoiseFold). Equal-power gains use the folded
    // amount (max from the 50% center up); the attack time sets a per-sample
    // ramp increment for the per-voice noise envelopes. Recomputed on slot
    // change so the trig / divide stay out of the audio path.
    if (force || combo_.HasChanged(SlotIndex(ComboSlot::NOISE_BLEND)))
    {
        const SummonerNoiseFold::Result nf =
            SummonerNoiseFold::Compute(pot_to_q15(combo_.GetValue(SlotIndex(ComboSlot::NOISE_BLEND))));
        noise_dry_gain_ = NoiseBlendGainDry(nf.amount);
        noise_wet_gain_ = NoiseBlendGainWet(nf.amount);
        const float samples = nf.attack * static_cast<float>(SAMPLE_RATE);
        noise_attack_inc_ = (samples < 1.0f)
                                ? Q15_MAX
                                : std::max<int32_t>(1, static_cast<int32_t>(Q15_MAX / samples));
    }

    // Groove: humanize (bottom zone) feeds the strum scheduler at the next
    // chord fire; swing/skip read groove_q15_ in the euclidean-hit path
    groove_q15_ = pot_to_q15(combo_.GetValue(SlotIndex(ComboSlot::GROOVE)));
    humanize_ = SummonerGroove::HumanizeFromQ15(groove_q15_);

    // Tremolo depth (SHIFT+BANK+POT_4): how far the gate's off phase drops.
    // This slot held the standalone envelope attack until Phase 10 folded attack
    // onto the primary POT_4 knob (2026-08-13); the ComboSlot index is reused
    // as-is, so kNumSlots and the EEPROM layout are unchanged. Defaults to full
    // depth so the effect is audible without hunting the fourth layer (the same
    // lesson as the Phase 8 FX B mix defaults).
    if (force || combo_.HasChanged(SlotIndex(ComboSlot::TREM_DEPTH)))
    {
        tremolo_.SetDepth(pot_to_q15(combo_.GetValue(SlotIndex(ComboSlot::TREM_DEPTH))));
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
        tremolo_.OnClockTick(); // re-lock the gate so it can't drift off the grid
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

    // Voice frequencies: the chord tones from the last fire, detune spread on
    // top (voice 0 stays true). The portamento glide-ratio term was removed
    // 2026-08-16 with the portamento retirement.
    for (size_t v = 0; v < kNumVoices; v++)
    {
        oscs_[v].SetFrequency(fmin(voice_freq_[v] * detune_mult_[v], kMaxPitchHz));
    }

    // Tremolo (SHIFT+POT_1): the knob quantizes to a ratio of the clock step
    // period, which Groove already measures — one source of truth. Set here at
    // UiLoop rate, never in the audio loop (the divide stays out of it).
    const SummonerTremolo::Setting trem =
        SummonerTremolo::FromQ15(pot_to_q15(pots_[Pot::TREMOLO]->GetValue()));
    trem_bass_exempt_ = trem.bass_exempt;
    tremolo_.SetPeriodFrames(groove_.GetPeriodFrames(), trem.ratio_index);

    // Root pitch 1V/oct out: the sounding root — quantized, detune never applies
    // to voice 0 — with C3 (kRootBase, the 0V-CV root) = 0V out.
    // DAC_1V is USB-power calibration only (HARDWARE.md); roots below C3 clamp
    // to 0V inside SetCvOut.
    const float root_octaves = std::log2(voice_freq_[0] / kRootBase);
    Kastle2::hw.SetCvOut(static_cast<int32_t>(root_octaves * static_cast<float>(DAC_1V) + 0.5f));

    // Volume: nothing to do here — Base's stock OUTPUT_GAIN owns SHIFT+POT_5
    // (digital gain + codec HP volume), applied in Base::AfterAudioLoop.

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

    // ShimmerReverb (Core 1): POT_5 primary is the dry↔wet + decay combo knob
    // (SummonerReverbBlend — 0 = fully dry, 50% = short/very-wet, 100% = long/
    // very-wet; moved to primary in the 2026-08-13 swap, volume now SHIFT+POT_5);
    // shimmer amount BANK+POT_5 (top crosses into the granular-extreme
    // cloud internally); interval SHIFT+POT_2 stepped over the 4 pitch zones. Set
    // here at UiLoop rate while Core 1 is idle between blocks — no cross-core race.
    const SummonerReverbBlend::Result blend =
        SummonerReverbBlend::Compute(pot_to_q15(pots_[Pot::REVERB_BLEND]->GetValue()));
    reverb_.SetDecay(blend.decay);
    reverb_wet_ = blend.wet;
    reverb_.SetShimmer(pot_to_q15(pots_[Pot::SHIMMER]->GetValue()));
    const int32_t interval_zone = pots_[Pot::INTERVAL]->GetMappedValue();
    reverb_.SetInterval(static_cast<ShimmerReverb::Interval>(interval_zone >= 0 ? interval_zone : 0));

    // FX B params (SHIFT+BANK fourth layer): the parameter knob (POT_5) follows
    // the selected effect — delay time in DELAY/BOTH, crush rate+depth in CRUSH;
    // the mix knob (POT_7) crossfades dry↔effected in SecondCoreProcess. Set at
    // UiLoop rate while Core 1 is idle between blocks (same no-race window the
    // reverb setters use). BOTH uses the param for delay time and the Init-time
    // moderate crush defaults.
    const int32_t fx_param = combo_.GetValue(SlotIndex(ComboSlot::FX_B_PARAM));
    fx_mix_ = pot_to_q15(combo_.GetValue(SlotIndex(ComboSlot::FX_B_MIX)));
    if (fx_b_ == FxB::DELAY || fx_b_ == FxB::BOTH)
    {
        const size_t delay_l = static_cast<size_t>(curve_map(fx_param, kMapFxDelay, MapClamp::TRUE));
        fx_delay_.SetDelay(delay_l, (delay_l * 3) / 4);
    }
    if (fx_b_ == FxB::CRUSH)
    {
        fx_crusher_.SetSampleRate(static_cast<q15_t>(curve_map(fx_param, kMapFxCrushRate, MapClamp::TRUE)));
        fx_crusher_.SetBitDepth(static_cast<uint32_t>(curve_map(fx_param, kMapFxCrushBits, MapClamp::TRUE)));
    }

    // Quantizer scale: BANK+POT_1 stepped over the default scale table
    const int32_t scale_index = pots_[Pot::SCALE]->GetMappedValue();
    quantizer_.SetScale(scale_index >= 0 ? scale_index : 0);

    // Folded envelope knob (POT_4, Phase 10): the CV (PARAM_3, attenuated by
    // SHIFT+POT_4) sums into the knob position *before* the fold, so modulation
    // traverses the same V-shaped decay + upper-half attack curve. Decay is
    // long→short→long about the 50% center; attack is instant across the lower
    // half and ramps in across the upper half (SummonerEnvelope, host-tested).
    // Same time/attack on all voices. Retires the standalone attack control
    // (SHIFT+BANK+POT_4) — see ApplyComboSlots.
    const int32_t decay_cv = apply_pot_mod(Kastle2::hw.GetAnalogValue(Hardware::AnalogInput::PARAM_3),
                                           pots_[Pot::LENGTH_ATTEN]->GetValue());
    const int32_t env_val = pots_[Pot::DECAY]->GetValue() + decay_cv;
    const SummonerEnvelope::Result env_times = SummonerEnvelope::Compute(pot_to_q15(env_val));
    for (size_t v = 0; v < kNumVoices; v++)
    {
        envs_[v].SetDecayTime(env_times.decay);
        envs_[v].SetAttackTime(env_times.attack);
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
