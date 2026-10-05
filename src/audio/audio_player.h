#pragma once

#include <string>
#include <sys/types.h>

enum class PlayerState {
    STOPPED,
    PLAYING,
    PAUSED
};

class AudioPlayer {

private:
    pid_t pid = -1;
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