# FluidLite CLI

FluidLite CLI is a command line front-end for offline MIDI rendering powered by the FluidLite synthesizer. It launches multiple worker threads, feeds MIDI events to the synth, and writes standard WAV files so that large batches of MIDIs can be rendered unattended.

## Build

```bash
mkdir build && cd build
cmake .. -DENABLE_SF3=ON -DSTB_VORBIS=ON -DFLUIDLITE_BUILD_CLI=ON
cmake --build . -j$(nproc)
```

- `ENABLE_SF3`/`STB_VORBIS` let the CLI decode SF3 SoundFonts.
- Set `CMAKE_BUILD_TYPE=Release` for best performance.

## Usage

```bash
./fluidlite-cli [options] <soundfont> <midi_files...>
```

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output <dir>` | Write WAVs to the provided directory | Input file directory |
| `-j, --jobs <n>` | Parallel render jobs | `1` |
| `-r, --rate <hz>` | Sample rate | `44100` |
| `-f, --format <fmt>` | Output format: `f32` or `s16` | `s16` |
| `-Z, --container <fmt>` | Output container: `wav`, `flac`, `ogg`, `aiff`, `au` | `wav` |
| `-g, --gain <value>` | Master gain (0.0–10.0) | `0.2` |
| `--no-reverb` | Disable the built-in reverb | Enabled |
| `--no-chorus` | Disable chorus | Enabled |
| `-q, --quiet` | Only show errors | Verbose progress |
| `-v, --verbose` | Detailed logging | Off |

### Examples

```bash
# Render a single MIDI
./fluidlite-cli soundfont.sf2 song.mid

# Batch rendering with four workers
./fluidlite-cli -j 4 -o output/ font.sf2 *.mid

# Render at 48 kHz with float32 output
./fluidlite-cli -r 48000 -f f32 font.sf2 track1.mid track2.mid
```

## Output

The CLI writes 16-bit PCM or 32-bit float audio files into the selected container (`wav`, `flac`, `ogg`, `aiff`, or `au`). The container maps to file extensions via the `--container` flag, and each output file inherits the MIDI stem name.

## Dependencies

- FluidLite library (+ optional SF3/STB Vorbis support)
- MiniMidi header-only MIDI parser (Git submodule under `minimidi/`)
- libsndfile for writing multiple container formats (added as a submodule)

## Format smoke test

Use `scripts/test_cli_formats.sh` to re-render `minimidi/example/mahler.mid` with your local `MuseScore_General.sf3` in supported containers. The script also rebuilds the CLI target so you can rerun the test after fixing issues. Note: this repository may be configured to skip or modify certain formats at build time depending on available system codecs.

## Performance Optimization

### Recommended Build Flags

For offline rendering you can enable more aggressive optimizations:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_C_FLAGS="-O3 -march=native -ffast-math" \
         -DCMAKE_CXX_FLAGS="-O3 -march=native -ffast-math" \
         -DFLUID_BUFSIZE=256 \
         -DENABLE_SF3=ON -DSTB_VORBIS=ON -DFLUIDLITE_BUILD_CLI=ON
```

### One-Command Timing & Profiling

`scripts/profile_render.sh` automates build, `time`, and `perf record` runs:

```bash
scripts/profile_render.sh /path/to/font.sf3 /path/to/song.mid [build-dir]

# Example
scripts/profile_render.sh ~/Music/MuseScore_General.sf3 \
    ~/code/symusic/tests/testcases/Multitrack_MIDIs/Mr.\ Blue\ Sky.mid

# Inspect perf results
perf report -i build-gcc/Mr.\ Blue\ Sky.perf.data \
    --call-graph=graph,0.5,caller --max-stack=15
```

The script drops `<midi>.time.log` and `<midi>.perf.data` under the build directory so you can compare multiple tweaks.

### Performance Results

| Build | Render Time | Speedup |
|-------|-------------|---------|
| Baseline (-O2, BUFSIZE=64) | 3.13 s | Baseline |
| **Recommended (-O3 -march=native -ffast-math, BUFSIZE=256)** | **2.13 s** | **+32%** |

> Test MIDI: 227 s track rendered with MuseScore_General.sf3

### Buffer Size Tuning

FluidLite defaults to `FLUID_BUFSIZE=64` samples per block for low-latency playback. Larger buffers work better for offline rendering because they reduce per-call overhead.

| `FLUID_BUFSIZE` | Impact |
|-----------------|--------|
| 64 (default) | Baseline |
| 256 | +8.7% |
| 512 / 1024 | +8.7% (no additional gain) |

### Aggressive Flag Notes

| Flag | Reasoning |
|------|-----------|
| `-O3` | Enables heavier inlining and loop optimizations |
| `-march=native` | Emits CPU-specific instructions (SSE/AVX, etc.) |
| `-ffast-math` | Loosens floating-point rules to unlock more optimizations |

> `-ffast-math` may slightly change floating-point behavior; artifacts are typically inaudible for rendering workloads.

### Hotspot Overview

`perf` sampling of a long MIDI render highlights the most expensive code paths:

| Function | Share | Notes |
|----------|-------|-------|
| `fluid_voice_effects` | 34% | Envelope/filter/effects processing |
| `fluid_dsp_float_interpolate_4th_order` | 19% | Fourth-order interpolation |
| `vorbis_decode_*` | ~15% | SF3 (Vorbis) decoding |
| `fluid_revmodel_processmix` | 4% | Reverb processing |

These are the prime targets for further optimizations (SIMD, cache-friendly rewrites, etc.).
