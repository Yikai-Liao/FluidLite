#ifndef FLUIDLITE_CLI_OUTPUT_FORMAT_HPP
#define FLUIDLITE_CLI_OUTPUT_FORMAT_HPP

#include <algorithm>
#include <cctype>
#include <string>

namespace fluidlite_cli {

enum class ContainerFormat {
    Unknown,
    Wav,
    Flac,
    Ogg,
    Aiff,
    Au,
    Mp3
};

inline std::string toLowerCase(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return input;
}

inline ContainerFormat containerFormatFromName(std::string name) {
    name = toLowerCase(std::move(name));

    if (name == "wav") {
        return ContainerFormat::Wav;
    }
    if (name == "flac") {
        return ContainerFormat::Flac;
    }
    if (name == "ogg" || name == "oga" || name == "vorbis") {
        return ContainerFormat::Ogg;
    }
    if (name == "aiff" || name == "aif") {
        return ContainerFormat::Aiff;
    }
    if (name == "au") {
        return ContainerFormat::Au;
    }
    if (name == "mp3") {
        return ContainerFormat::Mp3;
    }

    return ContainerFormat::Unknown;
}

inline ContainerFormat containerFormatFromExtension(std::string extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(0, 1);
    }
    return containerFormatFromName(std::move(extension));
}

inline const char* containerFormatExtension(ContainerFormat format) {
    switch (format) {
        case ContainerFormat::Flac:
            return ".flac";
        case ContainerFormat::Ogg:
            return ".ogg";
        case ContainerFormat::Aiff:
            return ".aiff";
        case ContainerFormat::Au:
            return ".au";
        case ContainerFormat::Mp3:
            return ".mp3";
        case ContainerFormat::Wav:
        case ContainerFormat::Unknown:
        default:
            return ".wav";
    }
}

inline const char* containerFormatName(ContainerFormat format) {
    switch (format) {
        case ContainerFormat::Flac:
            return "flac";
        case ContainerFormat::Ogg:
            return "ogg";
        case ContainerFormat::Aiff:
            return "aiff";
        case ContainerFormat::Au:
            return "au";
        case ContainerFormat::Mp3:
            return "mp3";
        case ContainerFormat::Wav:
        case ContainerFormat::Unknown:
        default:
            return "wav";
    }
}

} // namespace fluidlite_cli

#endif // FLUIDLITE_CLI_OUTPUT_FORMAT_HPP
