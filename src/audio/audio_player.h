#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "audio/circular_audio_buffer.h"

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
    ProcessId pid = 0; // proceso decodificador
#if defined(_WIN32)
    HANDLE process_handle = nullptr;
#else
    ProcessId output_pid_ = 0;
    int decoded_fd_ = -1;
    int output_fd_ = -1;
    std::thread producer_thread_;
    std::thread consumer_thread_;
    CircularAudioBuffer buffer_;
    std::atomic<bool> stopping_{false};
    // Decodificador de la SIGUIENTE pista, lanzado antes de que se necesite.
    ProcessId prefetch_pid_ = 0;
    int prefetch_fd_ = -1;
    std::string prefetch_path_;
#endif
    std::atomic<PlayerState> state{PlayerState::STOPPED};
    std::recursive_mutex mutex_;

    void refresh();
#if !defined(_WIN32)
    void produce_pcm();
    void consume_pcm();
    void stop_locked();
    ProcessId start_decoder(const std::string& path, int& read_fd);
    void drop_prefetch_locked();
#endif

public:
    AudioPlayer() = default;
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool play(const std::string& path);
    // Lanza ya el decodificador de 'path' (la siguiente pista) para que, al
    // reproducirla, arranque decodificado. No suena nada hasta play(path).
    bool prefetch(const std::string& path);
    bool pause();
    bool resume();
    bool stop();
    bool close();
    PlayerState getState();
};
