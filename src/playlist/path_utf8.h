#pragma once

#include <filesystem>
#include <string>

namespace playlist {

// std::filesystem::path <-> std::string en UTF-8, igual en Windows y Linux.
// AudioPlayer (Windows) espera UTF-8 y lo convierte con MultiByteToWideChar.
inline std::string to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

inline std::filesystem::path from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}  // namespace playlist
