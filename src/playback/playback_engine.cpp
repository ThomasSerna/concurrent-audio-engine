#include "playback/playback_engine.h"

#include <chrono>
#include <filesystem>
#include <utility>

#include "playlist/path_utf8.h"

using namespace std::chrono_literals;

PlaybackEngine::PlaybackEngine(playlist::Playlist& playlist) : playlist_(playlist) {}

PlaybackEngine::~PlaybackEngine() {
    shutdown();
}

void PlaybackEngine::set_redraw_callback(std::function<void()> callback) {
    redraw_ = std::move(callback);
}

void PlaybackEngine::start() {
    if (!worker_.joinable()) {
        worker_ = std::thread([this] { run(); });
    }
}

void PlaybackEngine::shutdown() {
    if (!worker_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(control_mutex_);
        shutting_down_ = true;
    }
    control_cv_.notify_all();
    playlist_.close();  // despierta wait_for_current()
    worker_.join();
    player_.close();
}

// ----------------------------------------------------------------------------
// Control (hilo de la UI)
// ----------------------------------------------------------------------------

void PlaybackEngine::play() {
    {
        std::lock_guard<std::mutex> guard(control_mutex_);
        user_stopped_ = false;
    }
    control_cv_.notify_all();
    request_redraw();
}

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

bool PlaybackEngine::pause() {
    const bool ok = player_.pause();
    request_redraw();
    return ok;
}

bool PlaybackEngine::resume() {
    const bool ok = player_.resume();
    request_redraw();
    return ok;
}

PlayerState PlaybackEngine::state() {
    return player_.getState();
}

bool PlaybackEngine::user_stopped() {
    std::lock_guard<std::mutex> guard(control_mutex_);
    return user_stopped_;
}

std::vector<std::string> PlaybackEngine::take_messages() {
    std::lock_guard<std::mutex> guard(messages_mutex_);
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}

// ----------------------------------------------------------------------------
// Hilo del motor
// ----------------------------------------------------------------------------

bool PlaybackEngine::wait_until_play_allowed() {
    std::unique_lock<std::mutex> lock(control_mutex_);
    control_cv_.wait(lock, [&] { return shutting_down_ || !user_stopped_; });
    return !shutting_down_;
}

void PlaybackEngine::run() {
    // wait_for_current() bloquea sin busy-wait y devuelve nullopt tras close().
    while (auto selection = playlist_.wait_for_current()) {
        if (!wait_until_play_allowed()) {
            break;
        }
        // Mientras esperabamos permiso, el usuario pudo haber hecho next/jump/remove.
        if (playlist_.selection_epoch() != selection->epoch) {
            continue;
        }

        const std::uint64_t stop_seen = stop_count_.load(std::memory_order_acquire);
        const std::string path = playlist::to_utf8(selection->track.path);

        if (!player_.play(path)) {
            post_message("No se pudo reproducir: " + path);
            // Evita quedarse pegado en una pista invalida: se salta a la siguiente.
            playlist_.on_track_finished(selection->epoch);
            request_redraw();
            continue;
        }
        request_redraw();

        // Preparacion de la siguiente pista: mientras suena la actual, se lanza ya
        // su decodificador. Si no existe, se avisa antes de llegar a ella.
        if (const auto next = playlist_.peek_next()) {
            if (!std::filesystem::is_regular_file(next->path)) {
                post_message("Aviso: la siguiente pista no existe: " +
                             playlist::to_utf8(next->path));
            } else {
                player_.prefetch(playlist::to_utf8(next->path));
            }
        }

        const auto started = std::chrono::steady_clock::now();
        bool natural_end = false;

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(control_mutex_);
                // Despierta de inmediato ante stop, cierre o cambio de seleccion.
                // El sondeo de 100 ms queda solo para detectar el fin natural de ffplay.
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
                break;  // el usuario hizo stop
            }
            if (playlist_.selection_epoch() != selection->epoch) {
                break;  // next / prev / jump / remove de la actual / qclear
            }
            if (player_.getState() == PlayerState::STOPPED) {
                natural_end = true;  // ffplay termino solo (-autoexit)
                break;
            }
        }

        if (natural_end) {
            if (std::chrono::steady_clock::now() - started < 300ms) {
                post_message("La pista termino de inmediato: " + path +
                             " (esta instalado ffplay y es un audio valido?)");
            }
            // Compare-and-advance: solo avanza si la seleccion no cambio.
            playlist_.on_track_finished(selection->epoch);
        } else {
            player_.stop();  // cambio de seleccion, stop del usuario o cierre
        }
        request_redraw();
    }
}

void PlaybackEngine::notify_change() {
    control_cv_.notify_all();
}

void PlaybackEngine::post_message(std::string message) {
    std::lock_guard<std::mutex> guard(messages_mutex_);
    messages_.push_back(std::move(message));
}

void PlaybackEngine::request_redraw() {
    if (redraw_) {
        redraw_();
    }
}
