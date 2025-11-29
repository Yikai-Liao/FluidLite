/**
 * @file main.cpp
 * @brief FluidLite CLI - MIDI offline renderer with multi-threading support
 * 
 * Usage: fluidlite-cli [options] <soundfont> <midi_files...>
 * 
 * Options:
 *   -o, --output <dir>     Output directory (default: same as input)
 *   -j, --jobs <n>         Number of parallel jobs (default: 1)
 *   -r, --rate <hz>        Sample rate (default: 44100)
 *   -f, --format <fmt>     Output format: f32 or s16 (default: f32)
 *   -g, --gain <value>     Master gain 0.0-10.0 (default: 0.2)
 *   --no-reverb            Disable reverb
 *   --no-chorus            Disable chorus
 *   -q, --quiet            Quiet mode (minimal output)
 *   -v, --verbose          Verbose output
 *   -h, --help             Show this help message
 *   --version              Show version information
 */

#include "midi_renderer.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <getopt.h>
#include <cstring>

#include "output_format.hpp"

namespace fs = std::filesystem;

// Version info
constexpr const char* VERSION = "1.0.0";

// ANSI color codes
namespace colors {
    const char* reset = "\033[0m";
    const char* red = "\033[31m";
    const char* green = "\033[32m";
    const char* yellow = "\033[33m";
    const char* blue = "\033[34m";
    const char* cyan = "\033[36m";
}

// Command line options
using fluidlite_cli::ContainerFormat;
using fluidlite_cli::containerFormatFromName;
using fluidlite_cli::containerFormatName;

struct Options {
    std::string soundfontPath;
    std::vector<std::string> midiFiles;
    std::string outputDir;
    int jobs = 1;
    uint32_t sampleRate = 44100;
    bool useFloat = false;  // Default to int16
    ContainerFormat containerFormat = ContainerFormat::Wav;
    float gain = 0.2f;
    bool reverb = true;
    bool chorus = true;
    bool quiet = false;
    bool verbose = false;
};

// Global state for progress reporting
std::mutex printMutex;
std::atomic<size_t> completedFiles{0};
std::atomic<size_t> failedFiles{0};

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <soundfont> <midi_files...>\n\n"
              << "FluidLite CLI - MIDI offline renderer with multi-threading support\n\n"
              << "Options:\n"
              << "  -o, --output <dir>     Output directory (default: same as input)\n"
              << "  -j, --jobs <n>         Number of parallel jobs (default: 1)\n"
              << "  -r, --rate <hz>        Sample rate (default: 44100)\n"
              << "  -f, --format <fmt>     Output sample format: f32 or s16 (default: s16)\n"
              << "  -Z, --container <fmt>  Output container (wav, flac, ogg, aiff, au)\n"
              << "                         (default: wav)\n"
              << "  -g, --gain <value>     Master gain 0.0-10.0 (default: 0.2)\n"
              << "  --no-reverb            Disable reverb\n"
              << "  --no-chorus            Disable chorus\n"
              << "  -q, --quiet            Quiet mode (minimal output)\n"
              << "  -v, --verbose          Verbose output\n"
              << "  -h, --help             Show this help message\n"
              << "  --version              Show version information\n\n"
              << "Output: WAV/FLAC/OGG/AIFF/AU files (16-bit PCM or 32-bit float)\n\n"
              << "Examples:\n"
              << "  " << programName << " soundfont.sf2 music.mid\n"
              << "  " << programName << " -j 4 -o output/ font.sf2 *.mid\n"
              << "  " << programName << " -r 48000 -f f32 font.sf2 track1.mid track2.mid\n";
}

void printVersion() {
    std::cout << "FluidLite CLI version " << VERSION << "\n"
              << "MIDI offline renderer using FluidLite synthesizer\n";
}

bool parseArgs(int argc, char* argv[], Options& opts) {
    static struct option longOptions[] = {
        {"output",     required_argument, nullptr, 'o'},
        {"jobs",       required_argument, nullptr, 'j'},
        {"rate",       required_argument, nullptr, 'r'},
        {"format",     required_argument, nullptr, 'f'},
        {"container",  required_argument, nullptr, 'Z'},
        {"gain",       required_argument, nullptr, 'g'},
        {"no-reverb",  no_argument,       nullptr, 'R'},
        {"no-chorus",  no_argument,       nullptr, 'C'},
        {"quiet",      no_argument,       nullptr, 'q'},
        {"verbose",    no_argument,       nullptr, 'v'},
        {"help",       no_argument,       nullptr, 'h'},
        {"version",    no_argument,       nullptr, 'V'},
        {nullptr,      0,                 nullptr, 0}
    };
    
    int opt;
    int optionIndex = 0;
    
    while ((opt = getopt_long(argc, argv, "o:j:r:f:g:qvhZ:", longOptions, &optionIndex)) != -1) {
        switch (opt) {
            case 'o':
                opts.outputDir = optarg;
                break;
            case 'j':
                opts.jobs = std::stoi(optarg);
                if (opts.jobs < 1) {
                    std::cerr << "Error: jobs must be >= 1\n";
                    return false;
                }
                break;
            case 'r':
                opts.sampleRate = std::stoul(optarg);
                if (opts.sampleRate < 8000 || opts.sampleRate > 192000) {
                    std::cerr << "Error: sample rate must be between 8000 and 192000\n";
                    return false;
                }
                break;
            case 'f':
                if (strcmp(optarg, "f32") == 0) {
                    opts.useFloat = true;
                } else if (strcmp(optarg, "s16") == 0) {
                    opts.useFloat = false;
                } else {
                    std::cerr << "Error: format must be 'f32' or 's16'\n";
                    return false;
                }
                break;
            case 'Z':
                opts.containerFormat = containerFormatFromName(optarg);
                if (opts.containerFormat == ContainerFormat::Unknown) {
                    std::cerr << "Error: unsupported container format: " << optarg << "\n";
                    return false;
                }
                break;
            case 'g':
                opts.gain = std::stof(optarg);
                if (opts.gain < 0.0f || opts.gain > 10.0f) {
                    std::cerr << "Error: gain must be between 0.0 and 10.0\n";
                    return false;
                }
                break;
            case 'R':
                opts.reverb = false;
                break;
            case 'C':
                opts.chorus = false;
                break;
            case 'q':
                opts.quiet = true;
                break;
            case 'v':
                opts.verbose = true;
                break;
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'V':
                printVersion();
                exit(0);
            default:
                return false;
        }
    }
    
    // Remaining arguments: soundfont and midi files
    if (optind >= argc) {
        std::cerr << "Error: missing soundfont file\n";
        return false;
    }
    
    opts.soundfontPath = argv[optind++];
    
    if (optind >= argc) {
        std::cerr << "Error: missing MIDI file(s)\n";
        return false;
    }
    
    while (optind < argc) {
        opts.midiFiles.push_back(argv[optind++]);
    }
    
    return true;
}

void workerThread(const Options& opts,
                  std::queue<std::string>& taskQueue,
                  std::mutex& queueMutex,
                  std::atomic<size_t>& taskIndex,
                  size_t totalFiles) {
    using namespace fluidlite_cli;
    
    // Create renderer for this thread
    AudioFormat format;
    format.sampleRate = opts.sampleRate;
    format.channels = 2;
    format.useFloat = opts.useFloat;
    
    std::unique_ptr<MidiRenderer> renderer;
    
    try {
        renderer = std::make_unique<MidiRenderer>(opts.soundfontPath, format);
        renderer->setGain(opts.gain);
        renderer->setReverbEnabled(opts.reverb);
        renderer->setChorusEnabled(opts.chorus);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(printMutex);
        std::cerr << colors::red << "Error creating renderer: " << e.what() 
                  << colors::reset << "\n";
        return;
    }
    
    while (true) {
        std::string midiFile;
        size_t currentIndex;
        
        // Get next task
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (taskQueue.empty()) {
                break;
            }
            midiFile = taskQueue.front();
            taskQueue.pop();
            currentIndex = ++taskIndex;  // Atomic increment for correct ordering
        }
        
        std::string outputPath = generateOutputPath(midiFile, opts.outputDir, opts.containerFormat);
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Note: Progress callback is disabled for multi-threaded mode as it causes output chaos
        auto result = renderer->renderToFile(midiFile, outputPath, nullptr);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Print result with lock to ensure complete line output
        {
            std::lock_guard<std::mutex> lock(printMutex);
            if (result.success) {
                completedFiles++;
                if (!opts.quiet) {
                    std::cout << colors::green << "✓ " << colors::reset
                              << "[" << currentIndex << "/" << totalFiles << "] "
                              << midiFile << " -> " << outputPath << " ("
                              << std::fixed << std::setprecision(1) 
                              << result.durationSeconds << "s audio, "
                              << duration.count() / 1000.0 << "s render)\n";
                }
            } else {
                failedFiles++;
                std::cerr << colors::red << "✗ " << colors::reset
                          << "[" << currentIndex << "/" << totalFiles << "] "
                          << midiFile << ": " << result.error << "\n";
            }
        }
    }
}

int main(int argc, char* argv[]) {
    Options opts;
    
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }
    
    // Verify soundfont exists
    if (!fs::exists(opts.soundfontPath)) {
        std::cerr << colors::red << "Error: SoundFont not found: " 
                  << opts.soundfontPath << colors::reset << "\n";
        return 1;
    }
    
    // Verify MIDI files exist
    std::vector<std::string> validMidiFiles;
    for (const auto& midiFile : opts.midiFiles) {
        if (fs::exists(midiFile)) {
            validMidiFiles.push_back(midiFile);
        } else {
            std::cerr << colors::yellow << "Warning: MIDI file not found: " 
                      << midiFile << colors::reset << "\n";
        }
    }
    
    if (validMidiFiles.empty()) {
        std::cerr << colors::red << "Error: No valid MIDI files found" 
                  << colors::reset << "\n";
        return 1;
    }
    
    // Create output directory if specified
    if (!opts.outputDir.empty()) {
        try {
            fs::create_directories(opts.outputDir);
        } catch (const std::exception& e) {
            std::cerr << colors::red << "Error creating output directory: " 
                      << e.what() << colors::reset << "\n";
            return 1;
        }
    }
    
    // Print summary
    if (!opts.quiet) {
        std::cout << colors::blue << "FluidLite CLI" << colors::reset << "\n"
                  << "SoundFont: " << opts.soundfontPath << "\n"
                  << "MIDI files: " << validMidiFiles.size() << "\n"
                  << "Jobs: " << opts.jobs << "\n"
                  << "Sample rate: " << opts.sampleRate << " Hz\n"
                  << "Format: " << (opts.useFloat ? "float32" : "int16") << "\n"
                  << "Container: " << containerFormatName(opts.containerFormat) << "\n"
                  << "Gain: " << opts.gain << "\n"
                  << "Reverb: " << (opts.reverb ? "enabled" : "disabled") << "\n"
                  << "Chorus: " << (opts.chorus ? "enabled" : "disabled") << "\n\n";
    }
    
    // Create task queue
    std::queue<std::string> taskQueue;
    for (const auto& file : validMidiFiles) {
        taskQueue.push(file);
    }
    
    std::mutex queueMutex;
    std::atomic<size_t> taskIndex{0};
    size_t totalFiles = validMidiFiles.size();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Start worker threads
    std::vector<std::thread> threads;
    int numThreads = std::min(opts.jobs, static_cast<int>(validMidiFiles.size()));
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(workerThread, std::ref(opts), 
                            std::ref(taskQueue), std::ref(queueMutex), 
                            std::ref(taskIndex), totalFiles);
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Print summary
    if (!opts.quiet) {
        std::cout << "\n" << colors::blue << "Summary:" << colors::reset << "\n"
                  << "  Completed: " << colors::green << completedFiles << colors::reset << "\n"
                  << "  Failed: " << colors::red << failedFiles << colors::reset << "\n"
                  << "  Total time: " << std::fixed << std::setprecision(2) 
                  << totalDuration.count() / 1000.0 << "s\n";
    }
    
    return failedFiles > 0 ? 1 : 0;
}
