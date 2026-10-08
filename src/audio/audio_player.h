#pragma once

#include <mutex>
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

    // El motor de reproduccion (hilo propio) y los comandos (hilo de la UI)
    // usan el mismo AudioPlayer. Recursivo porque play() llama a stop().
    std::recursive_mutex mutex_;

    void refresh();

public:
    bool play(const std::string& path);
    bool pause();
    bool resume();
    bool stop();

    bool close();

    PlayerState getState();
};