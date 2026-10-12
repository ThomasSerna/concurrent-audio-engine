//Include guard for reading the file only once
#pragma once

//Include of required tools
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

//Include of required files
#include "audio/circular_audio_buffer.h"

#if defined(_WIN32)
#include <windows.h>
using ProcessId = DWORD;
#else
#include <sys/types.h>
using ProcessId = pid_t;
#endif

//Possible states in which the player can be
enum class PlayerState {
    STOPPED,
    PLAYING,
    PAUSED
};

class AudioPlayer {
private:
    ProcessId pid = 0; //ffmpeg identifier number
#if defined(_WIN32)
    HANDLE process_handle = nullptr;
#else
    ProcessId output_pid_ = 0; //Number of the ffplay process
    int decoded_fd_ = -1;//Pipe for decoding process
    int output_fd_ = -1;//Pipe for receiving the decoded number
    std::thread producer_thread_; //Thread that move the sound
    std::thread consumer_thread_; //Thread that move the sound
    CircularAudioBuffer buffer_; //Buffer that moves in the section 4
    std::atomic<bool> stopping_{false}; //Flag to indicate process stops
    // An ffmpeg for the next song prepared
    ProcessId prefetch_pid_ = 0;
    int prefetch_fd_ = -1;
    std::string prefetch_path_;
#endif
    //Current state of the player
    std::atomic<PlayerState> state{PlayerState::STOPPED};
    std::recursive_mutex mutex_;//The recursive lock for the song

    void refresh();
#if !defined(_WIN32)
    void produce_pcm();
    void consume_pcm();
    void stop_locked();
    ProcessId start_decoder(const std::string& path, int& read_fd);
    void drop_prefetch_locked();
#endif

public:
    //Evites creating copies of the AudioPlayer
    //as this one has its lock
    AudioPlayer() = default;
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    //States that the player can have
    bool play(const std::string& path);    
    bool prefetch(const std::string& path);
    bool pause();
    bool resume();
    bool stop();
    bool close();
    PlayerState getState();
};
