#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "audio/audio_player.h"

int main() {
#if defined(_WIN32)
    std::cout << "SKIP: esta prueba de humo requiere POSIX/Linux\n";
    return 0;
#else
    // El backend SDL dummy permite probar el pipeline en CI/servidores sin tarjeta de audio.
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    const std::string path = "/tmp/concurrent-audio-smoke.wav";
    const std::string command = "ffmpeg -nostdin -v error -f lavfi -i sine=frequency=440:duration=2 -ar 44100 -ac 2 -y '" + path + "'";
    if (std::system(command.c_str()) != 0) {
        std::cerr << "SKIP/FAIL: no fue posible generar audio de prueba con ffmpeg\n";
        return 2;
    }
    AudioPlayer player;
    if (!player.play(path)) {
        std::cerr << "FAIL: play() no pudo iniciar\n";
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (player.getState() == PlayerState::STOPPED) {
        std::cerr << "FAIL: el proceso de audio terminó inmediatamente; revisa ffplay y el dispositivo de salida\n";
        player.stop();
        std::filesystem::remove(path);
        return 1;
    }
    if (!player.pause()) {
        std::cerr << "FAIL: pause() no pudo pausar el audio activo\n";
        player.stop();
        std::filesystem::remove(path);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!player.resume()) {
        std::cerr << "FAIL: resume() no pudo reanudar el audio pausado\n";
        player.stop();
        std::filesystem::remove(path);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    player.stop();
    if (player.getState() != PlayerState::STOPPED) {
        std::cerr << "FAIL: el estado no terminó en STOPPED\n";
        return 1;
    }
    std::filesystem::remove(path);
    std::cout << "PASS: inicio, pause, resume, stop y cierre\n";
    return 0;
#endif
}
