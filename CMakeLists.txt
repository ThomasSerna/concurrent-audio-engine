cmake_minimum_required(VERSION 3.20)

project(concurrent-audio-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)  # NUEVO: pthread

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v7.0.3
)
FetchContent_MakeAvailable(ftxui)

add_executable(
  concurrent-audio-engine
  src/main.cpp
  src/commands/basic_commands.cpp
  src/commands/commands.h
  src/audio/audio_player.cpp
  src/audio/audio_player.h
  src/commands/audio_commands.cpp
  src/playlist/playlist.cpp  # NUEVO
  src/playlist/playlist.h    # NUEVO
  src/playlist/rw_lock.h     # NUEVO
  src/playlist/track.h       # NUEVO
)

# NUEVO: permite escribir #include "playlist/playlist.h"
target_include_directories(
  concurrent-audio-engine
  PRIVATE
  src
)

target_link_libraries(
  concurrent-audio-engine
  PRIVATE
  Threads::Threads  # NUEVO
  ftxui::screen
  ftxui::dom
  ftxui::component
)