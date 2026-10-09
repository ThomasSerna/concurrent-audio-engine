cmake_minimum_required(VERSION 3.20)
project(concurrent-audio-engine LANGUAGES CXX)

if(WIN32)
  message(FATAL_ERROR
    "Este proyecto requiere POSIX/Linux (pthread, fork, pipes). "
    "En CLion configura un toolchain WSL (Ubuntu) y borra cmake-build-debug/.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(CTest)

option(BUILD_APPLICATION "Build the FTXUI terminal application" ON)

if(BUILD_APPLICATION)
  include(FetchContent)
  FetchContent_Declare(
          ftxui
          GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
          GIT_TAG v7.0.3
  )
  FetchContent_MakeAvailable(ftxui)

  add_executable(concurrent-audio-engine
          src/main.cpp
          src/commands/basic_commands.cpp
          src/commands/commands.h
          src/audio/audio_player.cpp
          src/audio/audio_player.h
          src/audio/circular_audio_buffer.h
          src/commands/audio_commands.cpp
          src/playlist/playlist.cpp
          src/playlist/playlist.h
          src/playlist/rw_lock.h
          src/playlist/track.h
          src/playlist/path_utf8.h
          src/playback/playback_engine.cpp
          src/playback/playback_engine.h
          src/commands/playlist_commands.cpp
  )
  target_include_directories(concurrent-audio-engine PRIVATE src)
  target_link_libraries(concurrent-audio-engine PRIVATE
          Threads::Threads
          ftxui::screen
          ftxui::dom
          ftxui::component
  )
endif()

if(BUILD_TESTING)
  add_executable(audio-buffer-test tests/audio_buffer_test.cpp)
  target_include_directories(audio-buffer-test PRIVATE src)
  target_link_libraries(audio-buffer-test PRIVATE Threads::Threads)
  add_test(NAME audio-buffer-test COMMAND audio-buffer-test)

  add_executable(playlist-stress-test
          tests/playlist_stress_test.cpp
          src/playlist/playlist.cpp
  )
  target_include_directories(playlist-stress-test PRIVATE src)
  target_link_libraries(playlist-stress-test PRIVATE Threads::Threads)
  add_test(NAME playlist-stress-test COMMAND playlist-stress-test)

  add_executable(audio-player-smoke-test
          tests/audio_player_smoke_test.cpp
          src/audio/audio_player.cpp
  )
  target_include_directories(audio-player-smoke-test PRIVATE src)
  target_link_libraries(audio-player-smoke-test PRIVATE Threads::Threads)
  add_test(NAME audio-player-smoke-test COMMAND audio-player-smoke-test)
endif()