#include "audio_player.h"

#if defined(_WIN32)
#include <cstdlib>
#include <io.h>
#include <string>
#include <vector>

namespace {

std::wstring to_wide_string(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring wide(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
    return wide;
}

bool file_exists_and_readable(const std::string& path) {
    const auto wide = to_wide_string(path);
    return wide.empty() ? false : (_waccess(wide.c_str(), 4) == 0);
}

} // namespace

void AudioPlayer::refresh() {
    if (pid <= 0 || process_handle == nullptr) {
        return;
    }

    const DWORD result = WaitForSingleObject(process_handle, 0);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(process_handle);
        process_handle = nullptr;
        pid = -1;
        state = PlayerState::STOPPED;
    }
}

bool AudioPlayer::play(const std::string& path) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (!file_exists_and_readable(path)) {
        return false;
    }

    if (pid > 0) {
        stop();
    }

    std::string command = "\"ffplay\" -nodisp -autoexit -loglevel error \"" + path + "\"";
    std::vector<char> buffer(command.begin(), command.end());
    buffer.push_back('\0');

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    if (!CreateProcessA(
            nullptr,
            buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        return false;
    }

    CloseHandle(process_info.hThread);
    process_handle = process_info.hProcess;
    pid = process_info.dwProcessId;
    state = PlayerState::PLAYING;
    return true;
}

bool AudioPlayer::stop() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (pid <= 0 || process_handle == nullptr) {
        return false;
    }

    if (state == PlayerState::PAUSED) {
        ResumeThread(process_handle);
    }

    if (TerminateProcess(process_handle, 1)) {
        WaitForSingleObject(process_handle, INFINITE);
    }

    CloseHandle(process_handle);
    process_handle = nullptr;
    pid = -1;
    state = PlayerState::STOPPED;
    return true;
}

bool AudioPlayer::resume() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (state != PlayerState::PAUSED || pid <= 0 || process_handle == nullptr) {
        return false;
    }

    if (ResumeThread(process_handle) == static_cast<DWORD>(-1)) {
        return false;
    }

    state = PlayerState::PLAYING;
    return true;
}

bool AudioPlayer::pause() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (state != PlayerState::PLAYING || pid <= 0 || process_handle == nullptr) {
        return false;
    }

    if (SuspendThread(process_handle) == static_cast<DWORD>(-1)) {
        return false;
    }

    state = PlayerState::PAUSED;
    return true;
}

bool AudioPlayer::close() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (pid <= 0 || process_handle == nullptr) {
        return true;
    }

    if (TerminateProcess(process_handle, 1)) {
        WaitForSingleObject(process_handle, INFINITE);
    }

    CloseHandle(process_handle);
    process_handle = nullptr;
    pid = -1;
    state = PlayerState::STOPPED;
    return true;
}

PlayerState AudioPlayer::getState() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();
    return state;
}

#else

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
    std::lock_guard<std::recursive_mutex> guard(mutex_);

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
    std::lock_guard<std::recursive_mutex> guard(mutex_);

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
    std::lock_guard<std::recursive_mutex> guard(mutex_);

    refresh();

    if (state != PlayerState::PAUSED)
        return false;

    if (kill(pid, SIGCONT) != 0)
        return false;

    state = PlayerState::PLAYING;

    return true;
}

bool AudioPlayer::pause() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);

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
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (pid <= 0) return true;

    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);

    pid = -1;
    state = PlayerState::STOPPED;
    return true;
}

PlayerState AudioPlayer::getState() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);

    refresh();

    return state;
}

#endif