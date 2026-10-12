//Include of the required files
#include "playlist/playlist.h"

//Include of the required toosl
#include <algorithm>
#include <iterator>
#include <shared_mutex>
#include <utility>

//Wrapper for encapsulating the files
//contained in this folder under the 
//name playlist
namespace playlist {

namespace {

//Allows to create a song where the 
//the title is the name of the file
//It also calculates its size to move
//it to its belonging path
Track make_track(TrackId id, std::filesystem::path path) {
    Track track;
    track.id = id;
    track.title = path.stem().string(); 
    track.path = std::move(path);
    return track;
}

//Takes care of finding a song by its id
//in case it doesnt find it. It returns
//it does not exist
template <typename Tracks>
auto find_track(Tracks& tracks, TrackId id) {
    return std::find_if(tracks.begin(), tracks.end(), [id](const Track& track) {
        return track.id == id;
    });
}

}  

//This is the wrapper for the write
//standard involving all of the needed processes
template <typename Fn>
auto Playlist::write(Fn&& fn) -> std::invoke_result_t<Fn, Mutation&> {
    using Result = std::invoke_result_t<Fn, Mutation&>;
    Mutation mutation;

    if constexpr (std::is_void_v<Result>) {
        {
            std::unique_lock guard(lock_); //Lock for the writing
            fn(mutation); //Makes the change
            publish_locked(mutation); //Increases the counters
        }  //After this the lock is closed

        //In case something happens wakes up
        //the other processes waiting
        if (mutation.touched) {
            notify_waiters();
        }
        //Same as before happens in the else
    } else {
        Result result{};
        {
            std::unique_lock guard(lock_);
            result = fn(mutation);
            publish_locked(mutation);
        }
        if (mutation.touched) {
            notify_waiters();
        }
        return result;
    }
}

//Increases version_ and selection_epoch_ if the current song gets changed
void Playlist::publish_locked(const Mutation& m) noexcept {
    if (m.touched) {
        version_.fetch_add(1, std::memory_order_release);
    }
    if (m.selection_changed) {
        selection_epoch_.fetch_add(1, std::memory_order_release);
    }
}

//Uses signal mutex to indicate the other tracks to continue
void Playlist::notify_waiters() {

        std::lock_guard<std::mutex> guard(signal_mutex_);
    }
    signal_cv_.notify_all();
}

//Indicates the current position of the song
std::optional<std::size_t> Playlist::current_index_locked() const {
    if (!current_) {
        return std::nullopt;
    }
    const auto it = find_track(tracks_, *current_);
    if (it == tracks_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - tracks_.begin());
}

//Returns a copy of the current song
std::optional<Track> Playlist::current_locked() const {
    if (const auto index = current_index_locked()) {
        return tracks_[*index];
    }
    return std::nullopt;
}

//Goes to the next song. In case there is not
//next song it ends up with no next song
std::optional<Track> Playlist::advance_locked(Mutation& m) {
    const auto index = current_index_locked();
    if (!index) {
        return std::nullopt;  // ya estamos en "fin de lista": nada que avanzar
    }
    m.mark_selection();
    const std::size_t next = *index + 1;
    if (next >= tracks_.size()) {
        current_.reset();  // se terminó la lista
        return std::nullopt;
    }
    current_ = tracks_[next].id;
    return tracks_[next];
}

//Writers 
//Gives a new id and adds it to the end of the playlist
//if there is not current song this turns into the new
//one
TrackId Playlist::add(std::filesystem::path path) {
    return write([&](Mutation& m) {
        const TrackId id = next_id_++;
        tracks_.push_back(make_track(id, std::move(path)));
        m.touched = true;
        if (!current_) {
            current_ = id;
            m.mark_selection();
        }
        return id;
    });
}

//This removes tracks by id, it it was the current one
//then it gets removed and the next one in line continues
//in case it is the only one it leaves the audio player
//without song
bool Playlist::remove(TrackId id) {
    return write([&](Mutation& m) {
        auto it = find_track(tracks_, id);
        if (it == tracks_.end()) {
            return false;
        }
        const bool was_current = current_ && *current_ == id;
        it = tracks_.erase(it);
        m.touched = true;
        if (was_current) {
            if (it != tracks_.end()) {
                current_ = it->id;  // la siguiente ocupa ahora el mismo índice
            } else {
                current_.reset();
            }
            m.mark_selection();
        }
        return true;
    });
}

//Goes to the next track by id
bool Playlist::move(TrackId id, std::size_t new_index) {
    return write([&](Mutation& m) {
        const auto it = find_track(tracks_, id);
        if (it == tracks_.end()) {
            return false;
        }
        const auto from = static_cast<std::size_t>(it - tracks_.begin());
        const std::size_t to = std::min(new_index, tracks_.size() - 1);
        if (from == to) {
            return true;
        }
        Track moved = std::move(*it);
        tracks_.erase(it);
        tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
        m.touched = true;  // el cursor sigue a la pista por id: no cambia la selección
        return true;
    });
}

//Removes all tracks
void Playlist::clear() {
    write([&](Mutation& m) {
        if (tracks_.empty()) {
            return;
        }
        tracks_.clear();
        m.touched = true;
        if (current_) {
            current_.reset();
            m.mark_selection();
        }
    });
}

//Chooses a new song as current one by id
bool Playlist::select(TrackId id) {
    return write([&](Mutation& m) {
        if (find_track(tracks_, id) == tracks_.end()) {
            return false;
        }
        current_ = id;
        m.mark_selection();
        return true;
    });
}

//Goest ot the next song by id
std::optional<Track> Playlist::advance() {
    return write([&](Mutation& m) { return advance_locked(m); });
}

//Goes back by 1 song by id
std::optional<Track> Playlist::previous() {
    return write([&](Mutation& m) -> std::optional<Track> {
        if (tracks_.empty()) {
            return std::nullopt;
        }
        const auto index = current_index_locked();
        const std::size_t target = !index ? tracks_.size() - 1 : (*index > 0 ? *index - 1 : 0);
        current_ = tracks_[target].id;
        m.mark_selection();
        return tracks_[target];
    });
}

//Advances to a nexxt song if is not finished
bool Playlist::on_track_finished(std::uint64_t epoch) {
    return write([&](Mutation& m) {
        if (selection_epoch_.load(std::memory_order_relaxed) != epoch) {
            return false;  
        }
        return advance_locked(m).has_value();
    });
}

//Readers

//Reads the current song
std::optional<Track> Playlist::current() const {
    std::shared_lock guard(lock_);
    return current_locked();
}

//Indicates the song selected currently
std::optional<Playlist::Selection> Playlist::current_selection() const {
    std::shared_lock guard(lock_);
    if (auto track = current_locked()) {
        return Selection{std::move(*track), selection_epoch_.load(std::memory_order_acquire)};
    }
    return std::nullopt;
}

//Sees the next song without moving itself
std::optional<Track> Playlist::peek_next() const {
    std::shared_lock guard(lock_);
    const auto index = current_index_locked();
    if (index && *index + 1 < tracks_.size()) {
        return tracks_[*index + 1];
    }
    return std::nullopt;
}

//Image of the current playlist details
Playlist::Snapshot Playlist::snapshot() const {
    std::shared_lock guard(lock_);
    return Snapshot{tracks_, current_, version_.load(std::memory_order_acquire)};
}

//Watches the real size of the playlist
std::size_t Playlist::size() const {
    std::shared_lock guard(lock_);
    return tracks_.size();
}

//Signaling
//Counts the counteres without lock
std::uint64_t Playlist::selection_epoch() const noexcept {
    return selection_epoch_.load(std::memory_order_acquire);
}
//This does the same as the selection_epoch
std::uint64_t Playlist::version() const noexcept {
    return version_.load(std::memory_order_acquire);
}

//Waits for version to change or closes the playlist
bool Playlist::wait_for_change(std::uint64_t seen_version) {
    std::unique_lock<std::mutex> guard(signal_mutex_);
    signal_cv_.wait(guard, [&] {
        return closed_.load(std::memory_order_acquire) ||
               version_.load(std::memory_order_acquire) != seen_version;
    });
    return !closed_.load(std::memory_order_acquire);
}

//Reads the version right now. If there is song continues 
//if not it goes to sleep the playlist
std::optional<Playlist::Selection> Playlist::wait_for_current() {
    for (;;) {

        const std::uint64_t seen = version();
        if (auto selection = current_selection()) {
            return selection;
        }
        if (!wait_for_change(seen)) {
            return std::nullopt;
        }
    }
}

//Indicates it is closed and wakes every next song
void Playlist::close() {
    closed_.store(true, std::memory_order_release);
    notify_waiters();
}

}  