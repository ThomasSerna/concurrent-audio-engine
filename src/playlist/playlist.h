//Include guard for reading the file only once
#pragma once

//Include of required tools
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

//Include of required programs
#include "playlist/rw_lock.h"
#include "playlist/track.h"

////Wrapper for encapsulating the files
//contained in this folder under the 
//name playlist
namespace playlist {
/*
This object defines the general structure
for how the playlist will work. Having in
mind that the playlist allows for adding
new music, erasing the one already in line
, and reproduce also music.It will implement
the system of locks developed previously in 
rw_lock.h and the basic structure of tracks 
named previously.This object will allow:
-Reading of a track by several threads at once
-Only 1 thread can manipulate writing of a song
-Delivers copies of the songs to evite 1 playlist
 affecting the functioning of other ones
-Songs can be saved by their id so in case of 
 reorganization the song is not lost
-The usage of locks makes it faster the reading
 and writing process
-selection-epoch allows to indicate when a song ends 
 to continue with the next one 
 */
class Playlist {
public:
    //Current song and the epoch
    //for indicating if the song
    //is done
    struct Selection {
        Track track;
        std::uint64_t epoch{};
    };

    //An instantaneous image of
    //the current situation of the
    //playlist involving tracks,
    //the current track being played
    //and the version indicates the 
    //number of the snapshot took in
    //that instant
    struct Snapshot {
        std::vector<Track> tracks;
        std::optional<TrackId> current;
        std::uint64_t version{};
    };

    //Indicates that cant create copies
    //of the actual playlist due the lock
    //implemented in the other files
    Playlist() = default;
    ~Playlist() = default;
    Playlist(const Playlist&) = delete;
    Playlist& operator=(const Playlist&) = delete;
    Playlist(Playlist&&) = delete;
    Playlist& operator=(Playlist&&) = delete;

    //Commands to write the playlist
    TrackId add(std::filesystem::path path);

    bool remove(TrackId id);

    bool move(TrackId id, std::size_t new_index);

    void clear();

    bool select(TrackId id);

    std::optional<Track> advance();

    std::optional<Track> previous();

    bool on_track_finished(std::uint64_t epoch);

    //Commands to read the playlist
    [[nodiscard]] std::optional<Track> current() const;
    [[nodiscard]] std::optional<Selection> current_selection() const;
    [[nodiscard]] std::optional<Track> peek_next() const;  
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::size_t size() const;

    //Signals for the playlist indicating information
    [[nodiscard]] std::uint64_t selection_epoch() const noexcept;
    [[nodiscard]] std::uint64_t version() const noexcept;
    [[nodiscard]] std::optional<Selection> wait_for_current();
    bool wait_for_change(std::uint64_t seen_version);
    void close();

private:
    //Indicates what was changed in the playlist
    struct Mutation {
        bool touched = false;
        bool selection_changed = false;
        void mark_selection() noexcept { touched = selection_changed = true; }
    };

    //Template that wraps all the changes
    template <typename Fn>
    auto write(Fn&& fn) -> std::invoke_result_t<Fn, Mutation&>;

    [[nodiscard]] std::optional<std::size_t> current_index_locked() const;
    [[nodiscard]] std::optional<Track> current_locked() const;
    std::optional<Track> advance_locked(Mutation& m);
    void publish_locked(const Mutation& m) noexcept;

    void notify_waiters();

    mutable RwLock lock_;
    //Template that wraps all the data
    std::vector<Track> tracks_;         
    std::optional<TrackId> current_;    
    TrackId next_id_{1};                

    //Atomic ounters to make sleep, wake up and
    //count the threads
    std::atomic<std::uint64_t> version_{0};
    std::atomic<std::uint64_t> selection_epoch_{0};
    std::atomic<bool> closed_{false};

    std::mutex signal_mutex_;
    std::condition_variable signal_cv_;
};

}  