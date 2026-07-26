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
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/synthesis/OscillatorQ15.hpp"
#include "common/dsp/utility/EdgeDetector.hpp"
#include "common/dsp/utility/Quantizer.hpp"
#include "SummonerChords.hpp"
#include "SummonerComboLayer.hpp"
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
     * @brief Called each time AudioLoop isn't busy.
     */
    void UiLoop();

    /**
     * @brief Called when the app is first loaded - initializes the memory values.
     */
    void MemoryInitialization() {}

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

    /**
     * @brief All FancyPot-managed knob functions (NORMAL / SHIFT / MODE layers
     *        per the CHORD-GEN.md knob table). POT_7 (LFO rate, SHIFT = tempo)
     *        stays with Base. The SHIFT+BANK fourth layer is app-managed —
     *        Hardware::Layer has no combo state, so FancyPot can't serve it.
     */
    enum class Pot
    {
        VOLUME,       ///< POT_5 primary: output volume
        PITCH_OFFSET, ///< POT_1 primary: chord root transpose (+-1 octave)
        DECAY,        ///< POT_4 primary: decay / note length
        VOICING,      ///< POT_2 primary: voicing sweep close -> open -> extended
        STRUM_SPEED,  ///< POT_3 primary: strum, 0 = block chord, max = slow arpeggio
        QUALITY,      ///< POT_6 primary: chord quality zones (major ... dim)
        PORTAMENTO,   ///< SHIFT+POT_1: portamento time (glide wired in Phase 4)
        STRUM_DIR,    ///< SHIFT+POT_3: strum direction (3-way stepped)
        LENGTH_ATTEN, ///< SHIFT+POT_4: LENGTH MOD CV attenuation
        CUTOFF,       ///< SHIFT+POT_6: filter cutoff (filter arrives in Phase 6)
        SCALE,        ///< BANK+POT_1: quantizer scale select
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
        HUMANIZE,    ///< POT_3: strum timing jitter per chord fire
        ATTACK,      ///< POT_4: envelope attack time
        FX_B_PARAM,  ///< POT_5: FX B parameter (stub until Phase 8)
        NOISE_BLEND, ///< POT_6: white-noise blend (stub until Phase 6)
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

    bool inited_ = false;

    // Voice engine
    std::array<OscillatorQ15, kNumVoices> oscs_;
    std::array<SummonerVoiceEnv, kNumVoices> envs_;
    SummonerStrum strum_;
    Quantizer quantizer_;

    // Chord-fire path
    EdgeDetector trigger_detect_ = EdgeDetector(EdgeDetector::Type::RISING);
    bool do_fire_ = false;
    bool sustain_gate_ = false;

    // Pots
    EnumArray<Pot, std::unique_ptr<FancyPot>> pots_;
    q15_t volume_ = 0;

    // SHIFT+BANK fourth layer
    SummonerComboLayer combo_;

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
