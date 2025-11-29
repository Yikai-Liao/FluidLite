/**
 * @file midi_renderer.hpp
 * @brief MIDI offline renderer using FluidLite and MiniMidi
 * 
 * This module provides functionality to render MIDI files to raw PCM audio
 * using the FluidLite software synthesizer.
 */

#ifndef FLUIDLITE_CLI_MIDI_RENDERER_HPP
#define FLUIDLITE_CLI_MIDI_RENDERER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>

// FluidLite C API
extern "C" {
#include "fluidlite.h"
}

// MiniMidi for MIDI parsing
#include "minimidi/MiniMidi.hpp"

#include "output_format.hpp"

#include <sndfile.h>

namespace fluidlite_cli {

/**
 * @brief Audio format options for rendering
 */
struct AudioFormat {
    uint32_t sampleRate = 44100;
    uint16_t channels = 2;
    bool useFloat = false;  // true for float32, false for int16 (default: int16)
};

/**
 * @brief Result of a render operation
 */
struct RenderResult {
    bool success = false;
    std::string error;
    std::string inputFile;
    std::string outputFile;
    double durationSeconds = 0.0;
    size_t samplesRendered = 0;
};

/**
 * @brief Progress callback type
 * @param currentSamples Current samples rendered
 * @param estimatedTotalSamples Estimated total samples (may change during rendering)
 */
using ProgressCallback = std::function<void(size_t currentSamples, size_t estimatedTotalSamples)>;

/**
 * @brief MIDI to audio renderer using FluidLite
 * 
 * This class encapsulates the FluidLite synthesizer and provides
 * methods to render MIDI files to raw PCM audio data.
 * 
 * Thread Safety:
 * - Each MidiRenderer instance is NOT thread-safe
 * - Multiple MidiRenderer instances can be used in parallel threads
 * - Create one instance per thread for parallel processing
 */
class MidiRenderer {
public:
    /**
     * @brief Construct a new MidiRenderer
     * @param soundfontPath Path to the SoundFont file (.sf2 or .sf3)
     * @param format Audio output format settings
     * @throws std::runtime_error if initialization fails
     */
    MidiRenderer(const std::string& soundfontPath, const AudioFormat& format = AudioFormat{});
    
    /**
     * @brief Destructor - cleans up FluidLite resources
     */
    ~MidiRenderer();
    
    // Non-copyable
    MidiRenderer(const MidiRenderer&) = delete;
    MidiRenderer& operator=(const MidiRenderer&) = delete;
    
    // Movable
    MidiRenderer(MidiRenderer&& other) noexcept;
    MidiRenderer& operator=(MidiRenderer&& other) noexcept;
    
    /**
     * @brief Render a MIDI file to a raw PCM file (streaming, low memory)
     * @param midiPath Path to the input MIDI file
     * @param outputPath Path to the output PCM file
     * @param callback Optional progress callback
     * @return RenderResult containing success status and details
     */
    RenderResult renderToFile(const std::string& midiPath, 
                              const std::string& outputPath,
                              ProgressCallback callback = nullptr);
    
    /**
     * @brief Get the current audio format settings
     */
    const AudioFormat& getFormat() const { return format_; }
    
    /**
     * @brief Set reverb parameters
     */
    void setReverb(double roomSize, double damping, double width, double level);
    
    /**
     * @brief Enable or disable reverb
     */
    void setReverbEnabled(bool enabled);
    
    /**
     * @brief Set chorus parameters
     */
    void setChorus(int voiceCount, double level, double speed, double depth, int type);
    
    /**
     * @brief Enable or disable chorus
     */
    void setChorusEnabled(bool enabled);
    
    /**
     * @brief Set master gain
     * @param gain Gain value (0.0 - 10.0)
     */
    void setGain(float gain);

private:
    fluid_settings_t* settings_ = nullptr;
    fluid_synth_t* synth_ = nullptr;
    int soundfontId_ = -1;
    AudioFormat format_;
    
    // Render buffer (reused across calls)
    static constexpr size_t RENDER_CHUNK_SIZE = 4096;
    std::vector<float> floatBuffer_;
    std::vector<int16_t> int16Buffer_;
    
    /**
     * @brief Send a MIDI message to the synth
     */
    void sendMidiMessage(const minimidi::Message<>& msg);
    
    /**
     * @brief Render samples and write to file
     * @return Number of samples rendered
     */
    size_t renderAndWrite(size_t numSamples, SNDFILE* sndFile);
    
    /**
     * @brief Reset the synthesizer state
     */
    void reset();
    
    /**
     * @brief Cleanup resources
     */
    void cleanup();
};

/**
 * @brief Helper function to generate output filename from input
 * @param inputPath Input MIDI file path
 * @param suffix Optional suffix (default: ".pcm")
 * @return Generated output path
 */
std::string generateOutputPath(const std::string& inputPath,
                               const std::string& outputDir,
                               ContainerFormat format);

} // namespace fluidlite_cli

#endif // FLUIDLITE_CLI_MIDI_RENDERER_HPP
