#!/usr/bin/env bash
# Builds and opens the demo host: BuildApp in a window with a loop playing
# through it, out of your speakers. No DAW.
#   ./start.sh              the built-in 145 BPM psytrance loop
#   ./start.sh track.wav    your file, looped
# CMake options for the first configure go in BA_CMAKE_ARGS, e.g.
#   BA_CMAKE_ARGS=-DFETCHCONTENT_SOURCE_DIR_JUCE=~/JUCE ./start.sh
set -euo pipefail

# The file argument is relative to where you are, not to the repo.
files=()
for f in "$@"; do
    [[ -f "$f" ]] || { echo "No such file: $f" >&2; exit 1; }
    files+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")")
done

cd "$(dirname "$0")"
if [[ ! -f build/build.ninja ]]; then
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ${BA_CMAKE_ARGS:-}
fi
cmake --build build --target buildapp_demo

exec "build/buildapp_demo_artefacts/Release/BuildApp Demo.app/Contents/MacOS/BuildApp Demo" ${files[@]+"${files[@]}"}
