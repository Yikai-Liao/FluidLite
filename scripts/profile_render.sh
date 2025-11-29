#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <soundfont.sf3> <midi.mid> [build_dir]" >&2
  exit 1
fi

SF3_PATH="$1"
MIDI_PATH="$2"
BUILD_DIR="${3:-build-gcc}"

CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_FLAGS="-O3 -march=native -ffast-math"
  -DCMAKE_CXX_FLAGS="-O3 -march=native -ffast-math"
  -DFLUID_BUFSIZE=256
  -DENABLE_SF3=ON
  -DSTB_VORBIS=ON
  -DFLUIDLITE_BUILD_CLI=ON
)

cmake -S . -B "$BUILD_DIR" "${CMAKE_FLAGS[@]}"
cmake --build "$BUILD_DIR"

pushd "$BUILD_DIR" >/dev/null

BASENAME=$(basename "$MIDI_PATH" .mid)
TIME_LOG="${BASENAME}.time.log"
PERF_DATA="${BASENAME}.perf.data"

/usr/bin/time -f '%E real, %P cpu' -o "$TIME_LOG" ./fluidlite-cli "$SF3_PATH" "$MIDI_PATH"

echo "Timing result recorded in $TIME_LOG"

echo "Collecting perf profile..."
perf record -o "$PERF_DATA" -g ./fluidlite-cli "$SF3_PATH" "$MIDI_PATH"
echo "Perf data saved to $BUILD_DIR/$PERF_DATA"
echo "Inspect with: perf report -i $BUILD_DIR/$PERF_DATA --call-graph=graph,0.5,caller --max-stack=15"

popd >/dev/null
