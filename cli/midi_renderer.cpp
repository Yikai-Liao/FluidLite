/**
 * @file midi_renderer.cpp
 * @brief Implementation of MIDI offline renderer
 */

#include "midi_renderer.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace fluidlite_cli {

// WAV file header structure (44 bytes)
#pragma pack(push, 1)
struct WavHeader {
    // RIFF chunk
    char riffId[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;  // File size - 8
    char waveId[4] = {'W', 'A', 'V', 'E'};
    
    // fmt chunk
    char fmtId[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // 1 = PCM, 3 = IEEE float
    uint16_t numChannels = 2;
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 0;     // sampleRate * numChannels * bitsPerSample/8
    uint16_t blockAlign = 0;   // numChannels * bitsPerSample/8
    uint16_t bitsPerSample = 16;
    
    // data chunk
    char dataId[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;     // Actual audio data size
};
#pragma pack(pop)

static_assert(sizeof(WavHeader) == 44, "WavHeader must be 44 bytes");

void writeWavHeader(std::ofstream& file, const AudioFormat& format, uint32_t dataSize) {
    WavHeader header;
    header.audioFormat = format.useFloat ? 3 : 1;  // 3 = IEEE float, 1 = PCM
    header.numChannels = format.channels;
    header.sampleRate = format.sampleRate;
    header.bitsPerSample = format.useFloat ? 32 : 16;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = dataSize;
    header.fileSize = 36 + dataSize;  // Total file size - 8 (RIFF header)
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
}

MidiRenderer::MidiRenderer(const std::string& soundfontPath, const AudioFormat& format)
    : format_(format)
{
    // Pre-allocate render buffers
    floatBuffer_.resize(RENDER_CHUNK_SIZE * format_.channels);
    int16Buffer_.resize(RENDER_CHUNK_SIZE * format_.channels);
    
    // Create settings
    settings_ = new_fluid_settings();
    if (!settings_) {
        throw std::runtime_error("Failed to create FluidLite settings");
    }
    
    // Configure settings
    fluid_settings_setnum(settings_, "synth.sample-rate", static_cast<double>(format_.sampleRate));
    fluid_settings_setint(settings_, "synth.audio-channels", format_.channels == 2 ? 1 : format_.channels);
    
    // Create synthesizer
    synth_ = new_fluid_synth(settings_);
    if (!synth_) {
        delete_fluid_settings(settings_);
        settings_ = nullptr;
        throw std::runtime_error("Failed to create FluidLite synthesizer");
    }
    
    // Load soundfont
    soundfontId_ = fluid_synth_sfload(synth_, soundfontPath.c_str(), 1);
    if (soundfontId_ == -1) {
        delete_fluid_synth(synth_);
        delete_fluid_settings(settings_);
        synth_ = nullptr;
        settings_ = nullptr;
        throw std::runtime_error("Failed to load SoundFont: " + soundfontPath);
    }
}

MidiRenderer::~MidiRenderer() {
    cleanup();
}

MidiRenderer::MidiRenderer(MidiRenderer&& other) noexcept
    : settings_(other.settings_)
    , synth_(other.synth_)
    , soundfontId_(other.soundfontId_)
    , format_(other.format_)
    , floatBuffer_(std::move(other.floatBuffer_))
    , int16Buffer_(std::move(other.int16Buffer_))
{
    other.settings_ = nullptr;
    other.synth_ = nullptr;
    other.soundfontId_ = -1;
}

MidiRenderer& MidiRenderer::operator=(MidiRenderer&& other) noexcept {
    if (this != &other) {
        cleanup();
        settings_ = other.settings_;
        synth_ = other.synth_;
        soundfontId_ = other.soundfontId_;
        format_ = other.format_;
        floatBuffer_ = std::move(other.floatBuffer_);
        int16Buffer_ = std::move(other.int16Buffer_);
        other.settings_ = nullptr;
        other.synth_ = nullptr;
        other.soundfontId_ = -1;
    }
    return *this;
}

void MidiRenderer::cleanup() {
    if (synth_) {
        if (soundfontId_ != -1) {
            fluid_synth_sfunload(synth_, soundfontId_, 1);
        }
        delete_fluid_synth(synth_);
        synth_ = nullptr;
    }
    if (settings_) {
        delete_fluid_settings(settings_);
        settings_ = nullptr;
    }
    soundfontId_ = -1;
}

void MidiRenderer::reset() {
    if (synth_) {
        // Turn off all notes on all channels using noteoff
        // Avoid system_reset which triggers "no preset found" warnings
        for (int ch = 0; ch < 16; ++ch) {
            for (int note = 0; note < 128; ++note) {
                fluid_synth_noteoff(synth_, ch, note);
            }
        }
    }
}

size_t MidiRenderer::renderAndWrite(size_t numSamples, std::ofstream& outFile) {
    if (numSamples == 0) return 0;
    
    size_t totalRendered = 0;
    
    while (numSamples > 0) {
        size_t toRender = std::min(numSamples, RENDER_CHUNK_SIZE);
        
        if (format_.useFloat) {
            fluid_synth_write_float(synth_, static_cast<int>(toRender),
                                    floatBuffer_.data(), 0, format_.channels,
                                    floatBuffer_.data(), 1, format_.channels);
            
            size_t bytes = toRender * format_.channels * sizeof(float);
            outFile.write(reinterpret_cast<const char*>(floatBuffer_.data()), bytes);
        } else {
            fluid_synth_write_s16(synth_, static_cast<int>(toRender),
                                  int16Buffer_.data(), 0, format_.channels,
                                  int16Buffer_.data(), 1, format_.channels);
            
            size_t bytes = toRender * format_.channels * sizeof(int16_t);
            outFile.write(reinterpret_cast<const char*>(int16Buffer_.data()), bytes);
        }
        
        totalRendered += toRender;
        numSamples -= toRender;
    }
    
    return totalRendered;
}

RenderResult MidiRenderer::renderToFile(const std::string& midiPath,
                                         const std::string& outputPath,
                                         ProgressCallback callback) {
    using namespace minimidi;
    
    RenderResult result;
    result.inputFile = midiPath;
    result.outputFile = outputPath;
    
    try {
        // Reset synth state
        reset();
        
        // Load MIDI file
        auto midi = MidiFile<>::from_file(midiPath);
        
        // Open output file
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            result.error = "Failed to open output file: " + outputPath;
            return result;
        }
        
        // Write placeholder WAV header (will update at the end)
        writeWavHeader(outFile, format_, 0);
        
        // Collect all events from all tracks with absolute time
        // Note: In MiniMidi, msg.time is already absolute time (not delta time)
        struct TimedEvent {
            uint32_t tick;
            size_t trackIdx;
            size_t msgIdx;
        };
        
        std::vector<TimedEvent> allEvents;
        
        // Pre-calculate total events for reservation
        size_t totalEventCount = 0;
        for (const auto& track : midi.tracks) {
            totalEventCount += track.messages.size();
        }
        allEvents.reserve(totalEventCount);
        
        // Collect events from all tracks
        for (size_t trackIdx = 0; trackIdx < midi.tracks.size(); ++trackIdx) {
            const auto& track = midi.tracks[trackIdx];
            for (size_t msgIdx = 0; msgIdx < track.messages.size(); ++msgIdx) {
                // msg.time is already absolute tick time in MiniMidi
                allEvents.push_back({track.messages[msgIdx].time, trackIdx, msgIdx});
            }
        }
        
        // Sort by tick (stable sort to preserve track order for same-tick events)
        std::stable_sort(allEvents.begin(), allEvents.end(),
                  [](const TimedEvent& a, const TimedEvent& b) {
                      return a.tick < b.tick;
                  });
        
        if (allEvents.empty()) {
            result.success = true;
            return result;
        }
        
        // Get ticks per quarter note
        uint16_t ticksPerQuarter = midi.ticks_per_quarter();
        
        // Default tempo: 120 BPM = 500000 microseconds per quarter note
        uint32_t microsecondsPerQuarter = 500000;
        
        // Estimate total duration in samples for progress reporting
        uint32_t lastEventTick = allEvents.back().tick;
        auto estimateTotalSamples = [&]() -> size_t {
            double secondsPerQuarter = microsecondsPerQuarter / 1000000.0;
            double secondsPerTick = secondsPerQuarter / ticksPerQuarter;
            double totalSeconds = lastEventTick * secondsPerTick + 2.0; // +2s for tail
            return static_cast<size_t>(totalSeconds * format_.sampleRate);
        };
        
        size_t estimatedTotal = estimateTotalSamples();
        size_t totalSamplesRendered = 0;
        uint32_t lastTick = 0;
        size_t lastProgressUpdate = 0;
        
        for (const auto& event : allEvents) {
            // Calculate samples to render for the time delta
            if (event.tick > lastTick) {
                uint32_t deltaTicks = event.tick - lastTick;
                
                // Calculate samples per tick with current tempo
                double secondsPerQuarter = microsecondsPerQuarter / 1000000.0;
                double secondsPerTick = secondsPerQuarter / ticksPerQuarter;
                size_t samplesToRender = static_cast<size_t>(deltaTicks * secondsPerTick * format_.sampleRate);
                
                if (samplesToRender > 0) {
                    totalSamplesRendered += renderAndWrite(samplesToRender, outFile);
                }
            }
            
            lastTick = event.tick;
            
            // Get the message
            const auto& msg = midi.tracks[event.trackIdx].messages[event.msgIdx];
            
            // Handle tempo changes
            if (msg.type() == MessageType::Meta) {
                const auto& meta = msg.cast<Meta>();
                if (meta.meta_type() == MetaType::SetTempo) {
                    const auto& setTempo = msg.cast<SetTempo>();
                    microsecondsPerQuarter = setTempo.tempo();
                    // Update estimate after tempo change
                    estimatedTotal = estimateTotalSamples();
                }
            } else {
                // Send MIDI message to synth
                sendMidiMessage(msg);
            }
            
            // Progress callback (throttled to avoid excessive calls)
            if (callback && (totalSamplesRendered - lastProgressUpdate > format_.sampleRate / 10)) {
                callback(totalSamplesRendered, estimatedTotal);
                lastProgressUpdate = totalSamplesRendered;
            }
        }
        
        // Render remaining audio (note release, reverb tail, etc.)
        // Add 2 seconds of tail
        size_t tailSamples = format_.sampleRate * 2;
        totalSamplesRendered += renderAndWrite(tailSamples, outFile);
        
        if (callback) {
            callback(totalSamplesRendered, totalSamplesRendered);
        }
        
        // Update WAV header with actual data size
        size_t bytesPerSample = format_.useFloat ? sizeof(float) : sizeof(int16_t);
        uint32_t dataSize = static_cast<uint32_t>(totalSamplesRendered * format_.channels * bytesPerSample);
        outFile.seekp(0, std::ios::beg);
        writeWavHeader(outFile, format_, dataSize);
        
        // Calculate statistics
        result.samplesRendered = totalSamplesRendered;
        result.durationSeconds = static_cast<double>(totalSamplesRendered) / format_.sampleRate;
        result.success = true;
        
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    
    return result;
}

void MidiRenderer::sendMidiMessage(const minimidi::Message<>& msg) {
    using namespace minimidi;
    
    uint8_t channel = msg.channel();
    
    switch (msg.type()) {
        case MessageType::NoteOn: {
            const auto& note = msg.cast<NoteOn>();
            uint8_t velocity = note.velocity();
            if (velocity > 0) {
                fluid_synth_noteon(synth_, channel, note.pitch(), velocity);
            } else {
                fluid_synth_noteoff(synth_, channel, note.pitch());
            }
            break;
        }
        
        case MessageType::NoteOff: {
            const auto& note = msg.cast<NoteOff>();
            fluid_synth_noteoff(synth_, channel, note.pitch());
            break;
        }
        
        case MessageType::ControlChange: {
            const auto& cc = msg.cast<ControlChange>();
            fluid_synth_cc(synth_, channel, cc.control_number(), cc.control_value());
            break;
        }
        
        case MessageType::ProgramChange: {
            const auto& pc = msg.cast<ProgramChange>();
            fluid_synth_program_change(synth_, channel, pc.program());
            break;
        }
        
        case MessageType::PitchBend: {
            const auto& pb = msg.cast<PitchBend>();
            // MIDI pitch bend is -8192 to 8191, FluidLite expects 0-16383
            int value = pb.pitch_bend() + 8192;
            fluid_synth_pitch_bend(synth_, channel, value);
            break;
        }
        
        case MessageType::ChannelAfterTouch: {
            fluid_synth_channel_pressure(synth_, channel, msg.data()[0]);
            break;
        }
        
        case MessageType::PolyphonicAfterTouch: {
            fluid_synth_key_pressure(synth_, channel, msg.data()[0], msg.data()[1]);
            break;
        }
        
        default:
            // Ignore other message types
            break;
    }
}

void MidiRenderer::setReverb(double roomSize, double damping, double width, double level) {
    if (synth_) {
        fluid_synth_set_reverb(synth_, roomSize, damping, width, level);
    }
}

void MidiRenderer::setReverbEnabled(bool enabled) {
    if (settings_) {
        fluid_settings_setstr(settings_, "synth.reverb.active", enabled ? "yes" : "no");
    }
}

void MidiRenderer::setChorus(int voiceCount, double level, double speed, double depth, int type) {
    if (synth_) {
        fluid_synth_set_chorus(synth_, voiceCount, level, speed, depth, type);
    }
}

void MidiRenderer::setChorusEnabled(bool enabled) {
    if (settings_) {
        fluid_settings_setstr(settings_, "synth.chorus.active", enabled ? "yes" : "no");
    }
}

void MidiRenderer::setGain(float gain) {
    if (synth_) {
        fluid_synth_set_gain(synth_, gain);
    }
}

std::string generateOutputPath(const std::string& inputPath, const std::string& suffix) {
    // Find the last dot in the filename
    size_t dotPos = inputPath.rfind('.');
    if (dotPos == std::string::npos) {
        return inputPath + suffix;
    }
    return inputPath.substr(0, dotPos) + suffix;
}

} // namespace fluidlite_cli
