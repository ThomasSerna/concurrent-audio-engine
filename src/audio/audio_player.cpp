#include "audio_player.h"

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void AudioPlayer::refresh() {

    if (pid <= 0)
        return;

    pid_t result = waitpid(
        pid,
        nullptr,
        WNOHANG
    );

    if (result == pid) {
        pid = -1;
        state = PlayerState::STOPPED;
    }
}

bool AudioPlayer::play(const std::string& path) {

    refresh();

    // Comprobar que el archivo existe
    if (access(path.c_str(), R_OK) != 0)
        return false;

    // Si habia otra cancion, detenerla
    if (pid > 0)
        stop();

    pid_t child = fork();

    if (child < 0)
        return false;

    if (child == 0) {

        execlp(
            "ffplay",
            "ffplay",
            "-nodisp",
            "-autoexit",
            "-loglevel",
            "error",
            path.c_str(),
            (char*) nullptr
        );

        perror("Error ejecutando ffplay");
        _exit(1);
    }

    pid = child;
    state = PlayerState::PLAYING;

    return true;
}

bool AudioPlayer::stop() {

    refresh();

    if (pid <= 0)
        return false;

    // Si estaba pausado, permitirle continuar
    // antes de terminarlo.
    if (state == PlayerState::PAUSED) {
        kill(pid, SIGCONT);
    }

    kill(pid, SIGTERM);

    waitpid(pid, nullptr, 0);

    pid = -1;
    state = PlayerState::STOPPED;

    return true;
}

bool AudioPlayer::resume() {

    refresh();

    if (state != PlayerState::PAUSED)
        return false;

    if (kill(pid, SIGCONT) != 0)
        return false;

    state = PlayerState::PLAYING;

    return true;
}

bool AudioPlayer::pause() {

    refresh();

    if (state != PlayerState::PLAYING)
        return false;

    if (kill(pid, SIGSTOP) != 0)
        return false;

    state = PlayerState::PAUSED;

    return true;
}

bool AudioPlayer::close()
{
    refresh();

    if (pid <= 0) return true;

    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);

    pid = -1;
    state = PlayerState::STOPPED;
    return true;
}

PlayerState AudioPlayer::getState() {

    refresh();

    return state;
}