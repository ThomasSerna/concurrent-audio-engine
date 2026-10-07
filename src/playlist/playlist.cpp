#include "playlist/playlist.h"

#include <algorithm>
#include <iterator>
#include <shared_mutex>
#include <utility>

namespace playlist {

namespace {

Track make_track(TrackId id, std::filesystem::path path) {
    Track track;
    track.id = id;
    track.title = path.stem().string();  // se calcula ANTES de mover la ruta
    track.path = std::move(path);
    return track;
}

// Sirve tanto para vectores const como no const.
template <typename Tracks>
auto find_track(Tracks& tracks, TrackId id) {
    return std::find_if(tracks.begin(), tracks.end(), [id](const Track& track) {
        return track.id == id;
    });
}

}  // namespace

// ----------------------------------------------------------------------------
// Infraestructura de escritura
// ----------------------------------------------------------------------------

template <typename Fn>
auto Playlist::write(Fn&& fn) -> std::invoke_result_t<Fn, Mutation&> {
    using Result = std::invoke_result_t<Fn, Mutation&>;
    Mutation mutation;

    if constexpr (std::is_void_v<Result>) {
        {
            std::unique_lock guard(lock_);
            fn(mutation);
            publish_locked(mutation);
        }  // <- lock_ liberado ANTES de tomar signal_mutex_ (regla 6)
        if (mutation.touched) {
            notify_waiters();
        }
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

void Playlist::publish_locked(const Mutation& m) noexcept {
    if (m.touched) {
        version_.fetch_add(1, std::memory_order_release);
    }
    if (m.selection_changed) {
        selection_epoch_.fetch_add(1, std::memory_order_release);
    }
}

void Playlist::notify_waiters() {
    {
        // Pasar por el mutex garantiza que un waiter que ya evaluó el predicado
        // pero aún no se durmió, recibirá la notificación (sin lost-wakeup).
        std::lock_guard<std::mutex> guard(signal_mutex_);
    }
    signal_cv_.notify_all();
}

// ----------------------------------------------------------------------------
// Helpers (requieren lock_ tomado)
// ----------------------------------------------------------------------------

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

std::optional<Track> Playlist::current_locked() const {
    if (const auto index = current_index_locked()) {
        return tracks_[*index];
    }
    return std::nullopt;
}

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

// ----------------------------------------------------------------------------
// Escritores
// ----------------------------------------------------------------------------

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

std::optional<Track> Playlist::advance() {
    return write([&](Mutation& m) { return advance_locked(m); });
}

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

bool Playlist::on_track_finished(std::uint64_t epoch) {
    return write([&](Mutation& m) {
        // La época sólo cambia con lock_ exclusivo, así que esta comparación es exacta.
        if (selection_epoch_.load(std::memory_order_relaxed) != epoch) {
            return false;  // alguien (Next/Prev/Remove...) ya movió la selección
        }
        return advance_locked(m).has_value();
    });
}

// ----------------------------------------------------------------------------
// Lectores
// ----------------------------------------------------------------------------

std::optional<Track> Playlist::current() const {
    std::shared_lock guard(lock_);
    return current_locked();
}

std::optional<Playlist::Selection> Playlist::current_selection() const {
    std::shared_lock guard(lock_);
    if (auto track = current_locked()) {
        return Selection{std::move(*track), selection_epoch_.load(std::memory_order_acquire)};
    }
    return std::nullopt;
}

std::optional<Track> Playlist::peek_next() const {
    std::shared_lock guard(lock_);
    const auto index = current_index_locked();
    if (index && *index + 1 < tracks_.size()) {
        return tracks_[*index + 1];
    }
    return std::nullopt;
}

Playlist::Snapshot Playlist::snapshot() const {
    std::shared_lock guard(lock_);
    return Snapshot{tracks_, current_, version_.load(std::memory_order_acquire)};
}

std::size_t Playlist::size() const {
    std::shared_lock guard(lock_);
    return tracks_.size();
}

// ----------------------------------------------------------------------------
// Señalización
// ----------------------------------------------------------------------------

std::uint64_t Playlist::selection_epoch() const noexcept {
    return selection_epoch_.load(std::memory_order_acquire);
}

std::uint64_t Playlist::version() const noexcept {
    return version_.load(std::memory_order_acquire);
}

bool Playlist::wait_for_change(std::uint64_t seen_version) {
    std::unique_lock<std::mutex> guard(signal_mutex_);
    signal_cv_.wait(guard, [&] {
        return closed_.load(std::memory_order_acquire) ||
               version_.load(std::memory_order_acquire) != seen_version;
    });
    return !closed_.load(std::memory_order_acquire);
}

std::optional<Playlist::Selection> Playlist::wait_for_current() {
    for (;;) {
        // IMPORTANTE: leer la versión ANTES de mirar el estado. Si un escritor
        // cambia algo entre medias, wait_for_change retorna de inmediato.
        const std::uint64_t seen = version();
        if (auto selection = current_selection()) {
            return selection;
        }
        if (!wait_for_change(seen)) {
            return std::nullopt;
        }
    }
}

void Playlist::close() {
    closed_.store(true, std::memory_order_release);
    notify_waiters();
}

}  // namespace playlist