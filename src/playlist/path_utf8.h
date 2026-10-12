//Include guard for reading the file only once
#pragma once

//Include of required tools
#include <filesystem>
#include <string>

//Wrapper for encapsulating the files
//contained in this folder under the 
//name playlist
namespace playlist {

//Inline was used to allow the creation
//of functions inside a .h file

//This takes care of taking a path of
//a track and then it turns it into a
//string using the utf 8 standard 
inline std::string to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

//This does the opposite of the last 
//function which is turning a text of
//standard utf8 into a path
inline std::filesystem::path from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} 
