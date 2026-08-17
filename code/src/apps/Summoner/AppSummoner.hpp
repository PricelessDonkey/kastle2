#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "common/EnumTools.hpp"
#include "common/core/App.hpp"
#include "common/core/Kastle2.hpp"
#include "common/controls/FancyMode.hpp"
#include "common/controls/FancyPot.hpp"
#include "common/dsp/effects/BitCrusher.hpp"
#include "common/dsp/effects/ShimmerReverb.hpp"
#include "common/dsp/effects/SoftClipper.hpp"
#include "common/dsp/effects/StereoDelay.hpp"
#include "common/dsp/filters/Svf.hpp"
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/synthesis/OscillatorQ15.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"
#include "common/dsp/utility/EdgeDetector.hpp"
#include "common/dsp/utility/EuclideanPattern.hpp"
#include "common/dsp/utility/Portamento.hpp"
#include "common/dsp/utility/Quantizer.hpp"
#include "SummonerChords.hpp"
#include "SummonerComboLayer.hpp"
#include "SummonerEnvelope.hpp"
#include "SummonerNoiseFold.hpp"
#include "SummonerSequencer.hpp"
#include "SummonerGroove.hpp"
#include "SummonerLfoShape.hpp"
#include "SummonerReverbBlend.hpp"
#include "SummonerStrum.hpp"
#include "SummonerVoiceEnv.hpp"

namespace kastle2
{

/**
 * @class AppSummoner
 * @ingroup apps
 * @brief Summoner — generative chord synthesizer (see CHORD-GEN.md design doc)
 * @author sam
 * @date 2026-07-17
 */
class AppSummoner : public virtual App
{
public:
    /**
     * @brief Initializes all the parameters, memory, etc.
     */
    void Init();

    /**
     * @brief Deinitializes the app, stops all effects, etc.
     */
    void DeInit();

    /**
     * @brief Called each interrupt loop. Implements all the audio processing.
     * @param input Input buffer.
     * @param output Output buffer.
     * @param size Number of sample pairs in the buffer (real size of the buffer is 2*size).
     */
    FASTCODE void AudioLoop(q15_t *input, q15_t *output, size_t size);

    /**
     * @brief Core 1 entry point — spins waiting for per-sample requests from
     *        Core 0 and runs the effects chain (clipper → Svf → ShimmerReverb).
     *        Registered via Kastle2::StartSecondCore in main.cpp.
     */
    FASTCODE void SecondCoreWorker();

    /**
     * @brief Called each time AudioLoop isn't busy.
     */
    void UiLoop();

    /**
     * @brief Called when the app is first loaded - initializes the memory values.
     */
    void MemoryInitialization();

    /**
     * @brief Returns the app ID.
     * @return The app ID.
     */
    uint8_t GetId()
    {
        return kAppId;
    }

private:
    static constexpr uint8_t kAppId = 0x11; ///< Community app ID range (see APP_LIST.md)

    static constexpr size_t kNumVoices = SummonerChords::kNumVoices;

    // EEPROM addresses for the stepped selectors (persist across power cycles;
    // the 0x11 app ID keeps them app-specific)
    static constexpr size_t kMemScale = Memory::ADDR_APP_SPACE + 0x0;    ///< Quantizer scale (BANK+POT_1)
    static constexpr size_t kMemStrumDir = Memory::ADDR_APP_SPACE + 0x1; ///< Strum direction (POT_3 primary since 2026-08-12)
    static constexpr size_t kMemFxMode = Memory::ADDR_APP_SPACE + 0x2;   ///< FX B selection (BANK press cycle)
    static constexpr size_t kMemWaveform = Memory::ADDR_APP_SPACE + 0x3; ///< Waveform slot value (SHIFT+BANK+POT_2)

    /**
     * @brief All FancyPot-managed knob functions (NORMAL / SHIFT / MODE layers
     *        per the CHORD-GEN.md knob table). POT_7 (LFO rate, SHIFT = tempo)
     *        stays with Base. The SHIFT+BANK fourth layer is app-managed —
     *        Hardware::Layer has no combo state, so FancyPot can't serve it.
     */
    enum class Pot
    {
        // No VOLUME slot: SHIFT+POT_5 is Base's stock main volume
        // (Feature::OUTPUT_GAIN, re-enabled 2026-08-14 — digital gain + codec
        // HP volume + EEPROM persistence, which the app's own gain lacked).
        PITCH_OFFSET, ///< POT_1 primary: chord root transpose (+-1 octave)
        DECAY,        ///< POT_4 primary: decay / note length
        VOICING,      ///< POT_2 primary: voicing sweep close -> open -> extended
        STRUM_DIR,    ///< POT_3 primary: strum direction, 6 zones + PARAM_2 CV (swapped with speed 2026-08-12)
        QUALITY,      ///< POT_6 primary: chord quality zones (major ... dim)
        PORTAMENTO,   ///< SHIFT+POT_1: portamento time on the chord root
        STRUM_SPEED,  ///< SHIFT+POT_3: strum speed, 0 = block chord, max = slow arpeggio (knob-only)
        LENGTH_ATTEN, ///< SHIFT+POT_4: LENGTH MOD CV attenuation
        CUTOFF,       ///< SHIFT+POT_6: filter cutoff
        SCALE,        ///< BANK+POT_1: quantizer scale select
        FILTER_ENV,   ///< BANK+POT_2: filter env amount (bipolar, center off)
        DENSITY,      ///< BANK+POT_3: euclidean density 0 -> K (0 = sequencer silent)
        LENGTH,       ///< BANK+POT_4: euclidean cycle length K (stepped, 2-16)
        RESONANCE,    ///< BANK+POT_6: filter resonance
        LFO_AMOUNT,   ///< BANK+POT_7: LFO TRI jack amplitude/polarity (attenuverter, center = flat 0V)
        REVERB_BLEND, ///< POT_5 primary: reverb combo — dry->wet then decay short->long (SummonerReverbBlend; moved to primary 2026-08-13 swap)
        SHIMMER,      ///< BANK+POT_5: shimmer amount (clean plate -> infinite shimmer, granular extreme at top)
        INTERVAL,     ///< SHIFT+POT_2: shimmer pitch interval (octave-down / fifth / octave / two-octave)
        COUNT
    };

    /**
     * @brief SHIFT+BANK fourth-layer slots (CHORD-GEN.md SHIFT+BANK column),
     *        index = pot number - 1.
     */
    enum class ComboSlot
    {
        DETUNE,      ///< POT_1: voice detune / spread
        WAVEFORM,    ///< POT_2: voice waveform select (sine/tri/saw/square)
        GROOVE,      ///< POT_3: humanize / swing / skip zones (expanded 2026-08-12)
        ATTACK,      ///< POT_4: (retired 2026-08-13 — attack folded onto primary POT_4; slot now free)
        FX_B_PARAM,  ///< POT_5: FX B parameter (stub until Phase 8)
        NOISE_BLEND, ///< POT_6: white-noise blend into the voices (equal-power)
        FX_B_MIX,    ///< POT_7: FX B mix (stub until Phase 8)
        COUNT
    };
    static_assert(static_cast<size_t>(ComboSlot::COUNT) == SummonerComboLayer::kNumSlots);

    /**
     * @brief Computes the chord from the current root/quality/voicing, sets the
     *        voice frequencies and schedules the strum. Called from UiLoop on a
     *        chord-fire event (clock tick or TRIG edge).
     */
    void FireChord();

    /**
     * @brief Runs the SHIFT+BANK fourth-layer state machine for one UI pass:
     *        feeds SummonerComboLayer, enforces the pot-movement-cancels-hold
     *        rule and latches the underlying layers' pots on release.
     */
    void ProcessComboLayer();

    /**
     * @brief Core 1 per-sample effects: reads the dry mono sample Core 0 wrote
     *        to the output buffer, runs clipper → Svf LP → ShimmerReverb, mixes
     *        the stereo wet tail over the dry, applies volume, writes back.
     * @param index Sample-pair index within the current block.
     */
    FASTCODE void SecondCoreProcess(size_t index);

    /**
     * @brief Applies the SHIFT+BANK slot values to the voice engine (waveform,
     *        detune spread, humanize, attack).
     * @param force Apply everything regardless of change flags (Init / after
     *              loading persisted values).
     */
    void ApplyComboSlots(bool force);

    /** @brief Shorthand for a ComboSlot's index into SummonerComboLayer. */
    static constexpr size_t SlotIndex(const ComboSlot slot)
    {
        return static_cast<size_t>(slot);
    }

    bool inited_ = false;

    // Voice engine
    std::array<OscillatorQ15, kNumVoices> oscs_;
    std::array<SummonerVoiceEnv, kNumVoices> envs_;
    SummonerStrum strum_;
    SummonerGroove groove_;
    Quantizer quantizer_;

    /// Per-voice noise sources for the SHIFT+BANK+POT_6 blend — distinct seeds
    /// so the blended noise isn't phase-locked across voices (CHORD-GEN.md
    /// Noise blend: identical streams read as a flangey artifact, not texture)
    std::array<WhiteNoise, kNumVoices> noises_;

    /// Equal-power blend gains (common/dsp/synthesis/NoiseBlend.hpp), computed
    /// on slot change in ApplyComboSlots — the trig stays out of AudioLoop
    q15_t noise_dry_gain_ = Q15_MAX;
    q15_t noise_wet_gain_ = 0;

    /// Folded noise-blend attack (Phase 10, SummonerNoiseFold): the noise gets
    /// its own per-voice attack ramp before the equal-power blend. Below the
    /// knob's 50% center the ramp is instant (immediate chiff); above it the
    /// noise swells in, the attack time scaling with knob position. Each voice's
    /// ramp resets on trigger and climbs by noise_attack_inc_ per sample to
    /// Q15_MAX (a huge inc = instant). Recomputed on slot change.
    std::array<int32_t, kNumVoices> noise_env_ = {}; ///< Per-voice noise attack ramp, 0..Q15_MAX
    int32_t noise_attack_inc_ = Q15_MAX;             ///< Per-sample ramp step (Q15_MAX = instant)

    // Effects chain — runs on Core 1 (Phase 7): mix (Core 0) -> SoftClipper ->
    // Svf LP -> ShimmerReverb -> volume. Core 0 writes the dry mono mix to the
    // output buffer; SecondCoreProcess reads it and overwrites with the wet mix.
    SoftClipper clipper_;
    Svf filter_;
    ShimmerReverb reverb_;

    /// Reverb dry↔wet crossfade from SHIFT+POT_5 (0 = dry, Q15_MAX = wet), set at
    /// UiLoop rate, read per-sample on Core 1 (see SummonerReverbBlend).
    q15_t reverb_wet_ = 0;

    // FX B slot (Phase 8): optional pre-reverb effect on Core 1, cycled by BANK
    // press. StereoDelay capped at kFxDelayMax samples (~500ms/ch, 88KB) so it
    // fits alongside the ~27KB reverb — never the 206KB default. The reverb is
    // independent and stays live in every FX B state, so Delay+reverb runs both
    // (echoes feed the shimmer tail). Param SHIFT+BANK+POT_5, mix SHIFT+BANK+POT_7.
    static constexpr size_t kFxDelayMax = 22000; ///< ~500ms/channel at 44kHz
    StereoDelay fx_delay_ = StereoDelay(kFxDelayMax);
    BitCrusher fx_crusher_;
    q15_t fx_mix_ = 0; ///< FX B wet/dry crossfade (SHIFT+BANK+POT_7), UiLoop → Core 1

    // Core 0 <-> Core 1 lock-step (WaveBard/FxWizard SecondCoreWorker pattern).
    q15_t *output_buffer_ = nullptr;             ///< Current block's output buffer (set by Core 0 each AudioLoop)
    size_t buffer_size_ = 0;                     ///< Sample-pairs in the current block
    size_t second_core_processed_samples_ = 0;   ///< Core 1's per-block progress counter

    // Chord-fire path
    EdgeDetector trigger_detect_ = EdgeDetector(EdgeDetector::Type::RISING);
    bool do_fire_ = false;
    bool clock_tick_ = false;
    bool groove_fire_ = false; ///< A swung (deferred) euclidean hit matured in AudioLoop
    bool sustain_gate_ = false;

    // Euclidean sequencer (steps on Base clock ticks; hits fire chords)
    EuclideanPattern euclid_;

    // PATTERN R (FEED_2) generator reset: previous tri-state, and the voicing
    // snap latch it sets (voicing forced to 0% until knob or CV movement)
    bool feed2_high_ = false;
    bool voicing_snap_ = false;
    int32_t voicing_cv_at_snap_ = 0;

    // Portamento on the chord root: the glide runs in log2-frequency space so
    // it sounds linear in pitch; voices scale by glided/target ratio
    Portamento portamento_;
    float root_pitch_target_ = 0.0f;                  ///< log2(target root Hz), set at fire time
    std::array<float, kNumVoices> voice_freq_ = {};   ///< Chord tone frequencies (pre-detune) from the last fire

    // Pots
    EnumArray<Pot, std::unique_ptr<FancyPot>> pots_;

    /// Signed LFO TRI scale from BANK+POT_7 (pot - POT_HALF, so -2047..+2048):
    /// positive = attenuated triangle, negative = inverted, 0 = flat 0V.
    /// Computed in UiLoop, applied to the jack each AudioLoop pass.
    int32_t lfo_amount_ = POT_HALF;

    /// LFO triangle reshaper for POT_7's outer 20% zones (wander / sample &
    /// hold). Rate pinning is applied to Base's LFO in UiLoop; the shape is
    /// applied to the TRI jack value each AudioLoop pass. See CHORD-GEN.md
    /// "LFO Shape Extremes".
    SummonerLfoShape lfo_shape_;
    bool lfo_last_sample_prev_ = false; ///< Edge-detects the LFO phase wrap (once-per-cycle redraw)
    int32_t lfo_rate_pot_ = POT_HALF;   ///< POT_7 NORMAL value, cached in UiLoop for the AudioLoop reshape
    WhiteNoise lfo_noise_;              ///< Dedicated random source for the wander/S&H redraws

    /// Mix envelope for ENV_OUT: sum of the 4 voice envelopes / 4, captured
    /// per sample in AudioLoop (ExampleSynth pattern), written in UiLoop
    q15_t env_mix_ = 0;

    // SHIFT+BANK fourth layer
    SummonerComboLayer combo_;
    int32_t waveform_zone_ = -1;                       ///< Cached waveform zone (-1 = not applied yet)
    std::array<float, kNumVoices> detune_mult_ = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Per-voice detune multipliers (root stays true)
    float humanize_ = 0.0f;                            ///< Strum jitter amount 0..1 (Groove bottom zone)
    q15_t groove_q15_ = 0;                             ///< Groove control value (SHIFT+BANK+POT_3)

    /**
     * @brief FX B slot states (Phase 8 wires the actual effects), cycled by a
     *        BANK press-release with no knob turn. The reverb is independent
     *        and never part of this cycle.
     */
    enum class FxB
    {
        OFF,
        DELAY,
        CRUSH,
        BOTH,
        COUNT
    };

    /** @brief FX B selector: BANK press-release cycles it; movement/timeout cancels. */
    FancyMode fx_mode_ = FancyMode(FancyMode::Config{
        .memory_addr = kMemFxMode,
        .modes_count = static_cast<uint32_t>(FxB::COUNT),
        .input_reading = FancyMode::InputReading::NONE});

    FxB fx_b_ = FxB::OFF;

    /** @brief LED feedback colors per FX B state (LED design pass is Phase 9). */
    EnumArray<FxB, uint32_t> fx_colors_ = {
        WS2812::GREEN,          // OFF — matches the current default LED
        WS2812::MEDIUM_CYAN,    // DELAY
        WS2812::ORANGE,         // CRUSH
        WS2812::MEDIUM_MAGENTA, // DELAY + CRUSH
    };
};
}
