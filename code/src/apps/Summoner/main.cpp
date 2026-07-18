
#include "common/core/Kastle2.hpp"
#include "apps/Summoner/AppSummoner.hpp"

using namespace kastle2;

/**
 * @file main.cpp
 * @brief Main entry point for Kastle 2 firmware.
 * @author sam
 * @date 2026-07-17
 */

AppSummoner app;

static void process_audio(q15_t *input, q15_t *output, size_t size)
{
    app.AudioLoop(input, output, size);
}

static void midi_callback(midi::Message *msg)
{
    app.MidiCallback(msg);
}

int main()
{
    // Initializes the hardware
    Kastle2::Init();

    // Register it with the Kastle2
    // Clears the EEPROM app space if the ID is different
    Kastle2::RegisterApp(&app);

    // Initialize the app
    app.Init();

    // Start I2S
    Kastle2::StartAudio(process_audio);

    // Set the MIDI callback
    Kastle2::SetAppMidiCallback(midi_callback);

    // Infinite program loop
    while (true)
    {
        Kastle2::ReadInputs();
        app.UiLoop();
    }

    return 0;
}
