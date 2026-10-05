cmake_minimum_required(VERSION 3.20)

project(concurrent-audio-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
        src/commands/commands.hpp
)

target_link_libraries(
        concurrent-audio-engine
        PRIVATE
        ftxui::screen
        ftxui::dom
        ftxui::component
)