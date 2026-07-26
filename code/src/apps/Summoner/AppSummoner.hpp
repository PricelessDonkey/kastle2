#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "common/EnumTools.hpp"
#include "common/core/App.hpp"
#include "common/core/Kastle2.hpp"
#include "common/controls/FancyPot.hpp"
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/synthesis/OscillatorQ15.hpp"
#include "common/dsp/utility/EdgeDetector.hpp"
#include "common/dsp/utility/Quantizer.hpp"
#include "SummonerChords.hpp"
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
     * @brief Computes the chord from the current root/quality/voicing, sets the
     *        voice frequencies and schedules the strum. Called from UiLoop on a
     *        chord-fire event (clock tick or TRIG edge).
     */
    void FireChord();

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
};
}
