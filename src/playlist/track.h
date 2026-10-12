//Include guard for reading the file only once
#pragma once

//Include of required tools
#include <cstdint>
#include <filesystem>
#include <string>

//Wrapper for encapsulating the files
//contained in this folder under the 
//name playlist
namespace playlist {

//This will be used to provide
//Unique ids to every song in 
//a playlist eviting consistency 
//issues
using TrackId = std::uint64_t;

//This struct defines that a track
//is made out of an identifier
//that was defined previously
//a path from which the song 
//comes from and a title
struct Track {
    TrackId id{};
    std::filesystem::path path;
    std::string title;
};

}