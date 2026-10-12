//Include guard to evite reading the project
//more than once
#pragma once

//Include of required tools
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//Include of required files
#include "audio/audio_player.h"
#include "playlist/playlist.h"

//Engine to execute the list of 
//songs that will be reproduced
class PlaybackEngine {
public:
    //constructor of the object casting a playlist object
    explicit PlaybackEngine(playlist::Playlist& playlist);
    //Destroyer of the object
    ~PlaybackEngine();

    //Indicates that an instance of PlayackEngine cant be
    //copied 
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    //Function that recalls the cal back function to clean the 
    //screen
    void set_redraw_callback(std::function<void()> callback);

    //Functions of the playback engine
    void start();
    void shutdown();  

    void play();
    bool stop();
    bool pause();
    bool resume();

    //Indicates the state of the audio player
    PlayerState state();
    //Indicates if the user stopped the player
    bool user_stopped();

    //Notifies a change in the state of 
    //the audio player
    void notify_change();

    //Receives the messages sent to the player
    std::vector<std::string> take_messages();

private:
    //Execute the audio player
    void run();
    //Waiting until the song can be executed
    bool wait_until_play_allowed();  
    //Send message of the player situation
    void post_message(std::string message);
    //Request to redraw entirety of console
    void request_redraw();

    //List of songs to play by reference
    playlist::Playlist& playlist_;
    //The instance of audio player
    AudioPlayer player_;
    //The execution thread
    std::thread worker_;
    //function to request redrawing screen
    std::function<void()> redraw_;

    //lock for the flags
    std::mutex control_mutex_;
    // alarm to wake up the flags  
    std::condition_variable control_cv_;
    //Request to stop the player
    bool user_stopped_ = false;
    //Request to end up the program
    bool shutting_down_ = false;
    //Allows to differentiate when 
    //the audio player was paused
    //from a song was finished
    std::atomic<std::uint64_t> stop_count_{0};

    //Locks and message errors to show in
    //console
    std::mutex messages_mutex_;
    std::vector<std::string> messages_;
};
