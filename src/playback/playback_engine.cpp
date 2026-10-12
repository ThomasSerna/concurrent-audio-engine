//Include of required files
#include "playback/playback_engine.h"
#include "playlist/path_utf8.h"

//Include of required tools
#include <chrono>
#include <filesystem>
#include <utility>

//Simplify the namespace instance
using namespace std::chrono_literals;

//Constructor for the playback engine
PlaybackEngine::PlaybackEngine(playlist::Playlist& playlist) : playlist_(playlist) {}

//Destructor for the playback engine
//using the shutdown fucntion to terminate
//everything
PlaybackEngine::~PlaybackEngine() {
    shutdown();
}

//Function to wipe out the console and redraw it
void PlaybackEngine::set_redraw_callback(std::function<void()> callback) {
    redraw_ = std::move(callback);
}

//Function to start the thread
//used in executing the audio player
void PlaybackEngine::start() {
    if (!worker_.joinable()) {
        worker_ = std::thread([this] { run(); });
    }
}

//Function to shutdown the audio player
void PlaybackEngine::shutdown() {
    if (!worker_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(control_mutex_);
        shutting_down_ = true;
    }
    control_cv_.notify_all();
    playlist_.close();  
    worker_.join();
    player_.close();
}


//Controls for the UI in the audio player
//Play a track
void PlaybackEngine::play() {
    {
        std::lock_guard<std::mutex> guard(control_mutex_);
        user_stopped_ = false;
    }
    control_cv_.notify_all();
    request_redraw();
}

//Stop a track
bool PlaybackEngine::stop() {
    {
        std::lock_guard<std::mutex> guard(control_mutex_);
        user_stopped_ = true;
        stop_count_.fetch_add(1, std::memory_order_acq_rel);
    }
    control_cv_.notify_all();
    const bool was_running = player_.stop();
    request_redraw();
    return was_running;
}

//Pause a track
bool PlaybackEngine::pause() {
    const bool ok = player_.pause();
    request_redraw();
    return ok;
}

//continue a song after it was paused
bool PlaybackEngine::resume() {
    const bool ok = player_.resume();
    request_redraw();
    return ok;
}

//Obtain the stage of the engine
//to view all possible options
PlayerState PlaybackEngine::state() {
    return player_.getState();
}

//To see if the user stopped the player
//or the songs all ended already
bool PlaybackEngine::user_stopped() {
    std::lock_guard<std::mutex> guard(control_mutex_);
    return user_stopped_;
}

//Take all the error messages
std::vector<std::string> PlaybackEngine::take_messages() {
    std::lock_guard<std::mutex> guard(messages_mutex_);
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}


//Wait to continue executing music
bool PlaybackEngine::wait_until_play_allowed() {
    std::unique_lock<std::mutex> lock(control_mutex_);
    control_cv_.wait(lock, [&] { return shutting_down_ || !user_stopped_; });
    return !shutting_down_;
}

//Run the playback engine
void PlaybackEngine::run() {
    while (auto selection = playlist_.wait_for_current()) {
        if (!wait_until_play_allowed()) {
            break;
        }
        //In case the user made a command in the player
        if (playlist_.selection_epoch() != selection->epoch) {
            continue;
        }

        const std::uint64_t stop_seen = stop_count_.load(std::memory_order_acquire);
        const std::string path = playlist::to_utf8(selection->track.path);

        if (!player_.play(path)) {
            post_message("No se pudo reproducir: " + path);
            //Jumps to the next song if the current song is unavailable
            playlist_.on_track_finished(selection->epoch);
            request_redraw();
            continue;
        }
        request_redraw();

        // Prepares the next song using prefetch and indicating the user
        if (const auto next = playlist_.peek_next()) {
            if (!std::filesystem::is_regular_file(next->path)) {
                post_message("Aviso: la siguiente pista no existe: " +
                             playlist::to_utf8(next->path));
            } else {
                player_.prefetch(playlist::to_utf8(next->path));
            }
        }

        //Chrono for counting the time 
        const auto started = std::chrono::steady_clock::now();
        bool natural_end = false;

        //For cycle that runs undefinitely to play song hearing the indications
        //of the user and stablishing locks for all the songs eviting concurrency issues
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(control_mutex_);
                control_cv_.wait_for(lock, 100ms, [&] {
                    return shutting_down_ ||
                           stop_count_.load(std::memory_order_acquire) != stop_seen ||
                           playlist_.selection_epoch() != selection->epoch;
                });
                if (shutting_down_) {
                    break;
                }
            }
            if (stop_count_.load(std::memory_order_acquire) != stop_seen) {
                break;  //Stopped the song
            }
            if (playlist_.selection_epoch() != selection->epoch) {
                break;  // next / prev / jump / remove of the current song / qclear
            }
            if (player_.getState() == PlayerState::STOPPED) {
                natural_end = true;  // Indicates if the song ends normally
                break;
            }
        }

        if (natural_end) {
            if (std::chrono::steady_clock::now() - started < 300ms) {
                post_message("La pista termino de inmediato: " + path +
                             " (esta instalado ffplay y es un audio valido?)");
            }
            // Compare-and-advance
            playlist_.on_track_finished(selection->epoch);
        } else {
            player_.stop();  //Selection change in case the user requested it
        }
        request_redraw();
    }
}

//Notifies changes to the next tracks
void PlaybackEngine::notify_change() {
    control_cv_.notify_all();
}

//Sends the message that qsa requested
void PlaybackEngine::post_message(std::string message) {
    std::lock_guard<std::mutex> guard(messages_mutex_);
    messages_.push_back(std::move(message));
}

//Redraws the console
void PlaybackEngine::request_redraw() {
    if (redraw_) {
        redraw_();
    }
}
