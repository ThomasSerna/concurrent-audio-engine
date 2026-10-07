#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#include "playlist/rw_lock.h"
#include "playlist/track.h"

namespace playlist {

// Lista de reproducción compartida y segura entre hilos (problema Lectores-Escritores).
//
// ============================ PROTOCOLO DE CONCURRENCIA ============================
//
// Recursos protegidos y su guarda:
//   tracks_, current_, next_id_  -> lock_ (pthread_rwlock, prioridad al escritor)
//   version_, selection_epoch_   -> escritos SOLO con lock_ en modo escritura;
//                                   son atómicos para poder leerse sin lock.
//   closed_                      -> atómico; se escribe en close().
//   signal_mutex_ + signal_cv_   -> sólo sirven para dormir/despertar hilos que
//                                   esperan cambios (sin espera activa).
//
// Reglas:
//  1. LECTORES (current, peek_next, snapshot, size, current_selection) toman
//     lock_ compartido: muchos en paralelo, nunca bloquean entre sí.
//  2. ESCRITORES (add, remove, move, clear, select, advance, previous,
//     on_track_finished) toman lock_ exclusivo.
//  3. Todo método devuelve COPIAS (Track por valor). Nadie retiene referencias
//     internas -> no hay use-after-free al eliminar/limpiar.
//  4. Dentro de una sección crítica sólo se usan helpers *_locked privados;
//     jamás se llama a un método público (evita re-adquirir lock_).
//  5. Jamás se hace E/S (abrir archivos, imprimir) con lock_ tomado. La
//     validación de rutas se hace antes de llamar a add().
//  6. ORDEN DE LOCKS: lock_ y signal_mutex_ NUNCA se mantienen a la vez.
//     Los escritores publican el cambio, SUELTAN lock_ y recién entonces toman
//     signal_mutex_ para notificar. No existe anidamiento -> no hay deadlock.
//  7. Sin lost-wakeup: el predicado de espera (version_/closed_) se evalúa bajo
//     signal_mutex_ y el notificador pasa por signal_mutex_ antes de notify_all.
//  8. Cursor por ID (no por índice): reordenar o insertar no desplaza la pista
//     actual. Si se elimina la pista actual, el cursor pasa a la siguiente.
//  9. Carrera "fin de pista natural" vs "usuario pulsa Next": el motor usa
//     on_track_finished(epoch), una operación compare-and-advance atómica que
//     sólo avanza si la selección no cambió desde que empezó la pista. Así no se
//     salta una canción por avanzar dos veces.
//
// ============================ USO DESDE EL MOTOR DE AUDIO ==========================
//
//     while (auto sel = playlist.wait_for_current()) {       // bloquea sin busy-wait
//         decoder_open(sel->track.path);
//         while (decoding) {
//             if (playlist.selection_epoch() != sel->epoch) break;   // Next/Prev/Remove/Clear
//             decode_one_chunk();
//         }
//         if (playlist.selection_epoch() == sel->epoch)
//             playlist.on_track_finished(sel->epoch);                // fin natural
//     }
//     // wait_for_current() devuelve nullopt tras playlist.close() -> el hilo termina.
//
class Playlist {
public:
    // Pista actual + época de selección, leídas de forma atómica entre sí.
    struct Selection {
        Track track;
        std::uint64_t epoch{};
    };

    // Vista consistente de toda la lista (una sola adquisición de lectura).
    struct Snapshot {
        std::vector<Track> tracks;
        std::optional<TrackId> current;
        std::uint64_t version{};
    };

    Playlist() = default;
    ~Playlist() = default;
    Playlist(const Playlist&) = delete;
    Playlist& operator=(const Playlist&) = delete;
    Playlist(Playlist&&) = delete;
    Playlist& operator=(Playlist&&) = delete;

    // ---------------------------- Escritores --------------------------------
    // Agrega al final. Si no había pista actual, la nueva pasa a serlo.
    TrackId add(std::filesystem::path path);

    // Elimina por id. Si era la actual, el cursor pasa a la siguiente (o a "fin").
    bool remove(TrackId id);

    // Mueve la pista a new_index (0-based, se recorta al último). El cursor la sigue.
    bool move(TrackId id, std::size_t new_index);

    // Vacía la lista y el cursor.
    void clear();

    // Salta a una pista concreta (siempre cambia la época, aunque ya fuese la actual).
    bool select(TrackId id);

    // "Next" del usuario: avanza incondicionalmente. Devuelve la nueva actual,
    // o nullopt si se llegó al final de la lista.
    std::optional<Track> advance();

    // "Previous" del usuario: retrocede una pista (en la primera, la reinicia).
    // Desde el estado "fin de lista" vuelve a la última pista.
    std::optional<Track> previous();

    // Fin natural de pista: avanza SOLO si la época no cambió. Devuelve true si avanzó.
    bool on_track_finished(std::uint64_t epoch);

    // ----------------------------- Lectores ---------------------------------
    [[nodiscard]] std::optional<Track> current() const;
    [[nodiscard]] std::optional<Selection> current_selection() const;
    [[nodiscard]] std::optional<Track> peek_next() const;  // para pre-cargar la siguiente
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::size_t size() const;

    // ------------------------- Señalización (sin lock) ----------------------
    // Cambia cuando la pista ACTUAL cambia (skip, remove de la actual, clear, select).
    [[nodiscard]] std::uint64_t selection_epoch() const noexcept;
    // Cambia con CUALQUIER modificación de la lista.
    [[nodiscard]] std::uint64_t version() const noexcept;

    // Bloquea (variable de condición, sin busy-wait) hasta que haya pista actual.
    // Devuelve nullopt si se llamó a close().
    [[nodiscard]] std::optional<Selection> wait_for_current();

    // Bloquea hasta que version() != seen_version. false si se llamó a close().
    bool wait_for_change(std::uint64_t seen_version);

    // Despierta a todos los que esperan; las esperas posteriores retornan de inmediato.
    void close();

private:
    // Banderas que cada escritor levanta dentro de la sección crítica.
    struct Mutation {
        bool touched = false;
        bool selection_changed = false;
        void mark_selection() noexcept { touched = selection_changed = true; }
    };

    // "Execute-around": toma lock exclusivo, ejecuta fn, publica versión/época,
    // SUELTA el lock y después notifica. Ningún escritor puede olvidar notificar.
    template <typename Fn>
    auto write(Fn&& fn) -> std::invoke_result_t<Fn, Mutation&>;

    // Helpers: exigen lock_ tomado por el llamador.
    [[nodiscard]] std::optional<std::size_t> current_index_locked() const;
    [[nodiscard]] std::optional<Track> current_locked() const;
    std::optional<Track> advance_locked(Mutation& m);
    void publish_locked(const Mutation& m) noexcept;

    void notify_waiters();

    mutable RwLock lock_;
    std::vector<Track> tracks_;         // guardado por lock_
    std::optional<TrackId> current_;    // guardado por lock_ (invariante: nullopt o id presente en tracks_)
    TrackId next_id_{1};                // guardado por lock_

    std::atomic<std::uint64_t> version_{0};
    std::atomic<std::uint64_t> selection_epoch_{0};
    std::atomic<bool> closed_{false};

    std::mutex signal_mutex_;
    std::condition_variable signal_cv_;
};

}  // namespace playlist