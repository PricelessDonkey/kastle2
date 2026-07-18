#!/usr/bin/env bash
# Zero-dependency build+run for the host-native DSP test suite. No cmake
# required — just a C++23 compiler. Mirrors CMakeLists.txt; keep them in
# sync if you add a source file or a new shim.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

CXX="${CXX:-c++}"
OUT="${1:-/tmp/kastle2_dsp_tests}"

"$CXX" -std=c++23 -Wall -Wextra -Wno-unused-parameter \
    -include algorithm \
    -I shims -I ../code/src -I ../code/src/common \
    dsp/*.cpp main.cpp \
    ../code/src/common/dsp/control/AdsrEnv.cpp \
    ../code/src/common/dsp/utility/Quantizer.cpp \
    ../code/src/common/dsp/effects/SoftClipper.cpp \
    ../code/src/common/dsp/synthesis/Oscillator.cpp \
    ../code/src/common/dsp/synthesis/OscillatorQ15.cpp \
    -o "$OUT"

exec "$OUT"
