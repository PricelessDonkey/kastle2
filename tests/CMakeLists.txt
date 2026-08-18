# Host-native test suite for the Kastle 2 shared DSP library.
#
# This is a deliberately separate, standalone CMake project — it does NOT
# include the top-level code/CMakeLists.txt, which is hard-wired to the
# RP2040 cross-toolchain via pico_sdk_import.cmake. That means:
#
#   - No arm-none-eabi-gcc, no Pico SDK, no board files required.
#   - Builds and runs in under a second with your system compiler.
#   - Only DSP modules with no (or shimmable) hardware dependencies can be
#     tested this way. See shims/ for the handful of pico-sdk headers that
#     get stubbed out, and README.md for what's in/out of scope.
#
# Usage:
#   cmake -S tests -B tests/build
#   cmake --build tests/build
#   ./tests/build/dsp_tests

cmake_minimum_required(VERSION 3.16)
project(kastle2_dsp_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(FIRMWARE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../code/src)

# shims/ is listed first so "pico/stdlib.h", "I2S.hpp", and "hardware/sync.h"
# resolve to our stand-ins instead of failing to find the real RP2040 headers.
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/shims
    ${FIRMWARE_SRC}
    ${FIRMWARE_SRC}/common
)

# DSP module sources under test. Add to this list as new modules gain tests —
# see README.md for the checklist of what's required for a module to be
# host-testable at all.
set(DSP_SOURCES
    ${FIRMWARE_SRC}/common/dsp/control/AdsrEnv.cpp
    ${FIRMWARE_SRC}/common/dsp/utility/Quantizer.cpp
    ${FIRMWARE_SRC}/common/dsp/effects/SoftClipper.cpp
    ${FIRMWARE_SRC}/common/dsp/effects/ShimmerReverb.cpp
    ${FIRMWARE_SRC}/common/dsp/synthesis/Oscillator.cpp
    ${FIRMWARE_SRC}/common/dsp/synthesis/OscillatorQ15.cpp
    ${FIRMWARE_SRC}/common/dsp/filters/Svf.cpp
)

set(TEST_SOURCES
    dsp/test_adsr_env.cpp
    dsp/test_allpass_precision.cpp
    dsp/test_quantizer.cpp
    dsp/test_soft_clipper.cpp
    dsp/test_shimmer_reverb.cpp
    dsp/test_slewer.cpp
    dsp/test_oscillator.cpp
    dsp/test_noise_blend.cpp
    dsp/test_euclidean_pattern.cpp
    dsp/test_combo_layer.cpp
    dsp/test_summoner_groove.cpp
    dsp/test_summoner_lfo_shape.cpp
    dsp/test_summoner_chords.cpp
    dsp/test_summoner_envelope.cpp
    dsp/test_summoner_noise_fold.cpp
    dsp/test_summoner_strum_sync.cpp
    dsp/test_summoner_tremolo.cpp
    dsp/test_summoner_scales.cpp
    dsp/test_summoner_sequencer.cpp
    dsp/test_summoner_strum.cpp
    dsp/test_summoner_timbre.cpp
    dsp/test_summoner_voice_env.cpp
    main.cpp
)

add_executable(dsp_tests ${DSP_SOURCES} ${TEST_SOURCES})
target_compile_options(dsp_tests PRIVATE -Wall -Wextra -Wno-unused-parameter
    # Quantizer.cpp uses std::max/std::min without including <algorithm>.
    # arm-none-eabi-g++ tolerates it (pulled in transitively by something
    # else in that toolchain's headers); host libc++/libstdc++ don't.
    # Genuine upstream portability gap, worth a small PR someday — force
    # the include here rather than patching the vendored source.
    -include algorithm
)

enable_testing()
add_test(NAME dsp_tests COMMAND dsp_tests)
