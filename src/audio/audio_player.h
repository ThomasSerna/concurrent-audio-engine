#pragma once

#include <string>

#if defined(_WIN32)
#include <windows.h>
using ProcessId = DWORD;
#else
#include <sys/types.h>
using ProcessId = pid_t;
#endif

enum class PlayerState {
    STOPPED,
    PLAYING,
    PAUSED
};

class AudioPlayer {

private:
    ProcessId pid = 0;
#if defined(_WIN32)
    HANDLE process_handle = nullptr;
#endif
    PlayerState state = PlayerState::STOPPED;

    void refresh();

public:
    bool play(const std::string& path);
    bool pause();
    bool resume();
    bool stop();

    bool close();

    PlayerState getState();
};