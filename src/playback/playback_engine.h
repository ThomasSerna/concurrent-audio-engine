#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio_player.h"
#include "playlist/playlist.h"

// Motor de reproduccion: hilo propio que consume la Playlist.
//
// Es el "USO DESDE EL MOTOR DE AUDIO" descrito en playlist.h:
//   - bloquea en playlist.wait_for_current() (sin busy-wait),
//   - arranca la pista actual con AudioPlayer (ffplay),
//   - vigila si el usuario cambio la seleccion (selection_epoch) o si la pista
//     termino sola (proceso ffplay finalizado) y entonces llama a
//     playlist.on_track_finished(epoch), que avanza solo si nadie se adelanto.
//
// Hilos: el hilo de la UI llama a play/stop/pause/resume/take_messages; el hilo
// del motor llama a AudioPlayer y a la Playlist. AudioPlayer tiene su propio
// mutex, y la Playlist es segura entre hilos por diseno.
class PlaybackEngine {
public:
    explicit PlaybackEngine(playlist::Playlist& playlist);
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // Se invoca desde el hilo del motor cuando cambia algo visible (nueva pista,
    // fin de pista, error). Debe llamarse ANTES de start().
    void set_redraw_callback(std::function<void()> callback);

    void start();
    void shutdown();  // idempotente: cierra la playlist, une el hilo, mata ffplay

    // Quita el estado "detenido por el usuario": el motor reproduce la pista actual.
    void play();
    // Detiene la pista y NO avanza. Devuelve false si no habia nada sonando.
    bool stop();
    bool pause();
    bool resume();

    PlayerState state();
    bool user_stopped();

    // Mensajes del motor (errores, etc.) para volcarlos a la consola de la UI.
    std::vector<std::string> take_messages();

private:
    void run();
    bool wait_until_play_allowed();  // false si se esta cerrando
    void post_message(std::string message);
    void request_redraw();

    playlist::Playlist& playlist_;
    AudioPlayer player_;
    std::thread worker_;
    std::function<void()> redraw_;

    std::mutex control_mutex_;  // protege user_stopped_ y shutting_down_
    std::condition_variable control_cv_;
    bool user_stopped_ = false;
    bool shutting_down_ = false;
    // Sube en cada stop(): permite distinguir "stop del usuario" de "fin natural"
    // aunque el usuario haga stop() + play() entre dos sondeos del motor.
    std::atomic<std::uint64_t> stop_count_{0};

    std::mutex messages_mutex_;
    std::vector<std::string> messages_;
};
