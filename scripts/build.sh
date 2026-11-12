#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake_bin="${CMAKE_BIN:-cmake}"
configure=(-S . -B build -DCMAKE_BUILD_TYPE=Release -DVULTUR_BUILD_PREVIEW=ON)
if [[ -n "${JUCE_SOURCE:-}" ]]; then
    configure+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$JUCE_SOURCE")
fi
"$cmake_bin" "${configure[@]}"
"$cmake_bin" --build build --config Release --parallel "${BUILD_JOBS:-4}"
"$cmake_bin" -E chdir build "${CTEST_BIN:-$(dirname "$(command -v "$cmake_bin")")/ctest}" -C Release --output-on-failure
if [[ "$(uname -s)" == Darwin ]]; then
    build/VulturPreview_artefacts/Release/"Vultur Preview.app"/Contents/MacOS/"Vultur Preview"
fi
