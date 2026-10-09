#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

#include "audio/circular_audio_buffer.h"

int main() {
    constexpr std::size_t total = 2 * 1024 * 1024 + 137;
    std::vector<char> source(total);
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<char>((i * 37U + 11U) % 251U);
    std::vector<char> destination(total);
    CircularAudioBuffer buffer(4093); // tamaño no potencia de dos: prueba el wrap-around
    std::atomic<bool> writer_ok{false};

    std::thread producer([&] {
        writer_ok = buffer.write(source.data(), source.size());
        buffer.close();
    });
    std::thread consumer([&] {
        std::size_t offset = 0;
        while (offset < destination.size()) {
            const auto amount = buffer.read(destination.data() + offset, 733);
            if (amount == 0) break;
            offset += amount;
        }
        if (offset != destination.size()) destination.resize(offset);
    });
    producer.join();
    consumer.join();

    if (!writer_ok || destination.size() != source.size() || destination != source) {
        std::cerr << "FAIL: datos perdidos, alterados o productor bloqueado\n";
        return 1;
    }
    // Cerrar debe despertar también a un productor bloqueado por búfer lleno.
    CircularAudioBuffer cancellation(16);
    std::vector<char> oversized(4096, 'x');
    std::atomic<bool> cancelled_writer_returned{false};
    std::thread blocked_writer([&] {
        const bool result = cancellation.write(oversized.data(), oversized.size());
        cancelled_writer_returned = !result;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cancellation.close();
    blocked_writer.join();
    if (!cancelled_writer_returned) {
        std::cerr << "FAIL: close() no canceló al productor bloqueado\n";
        return 1;
    }

    std::cout << "PASS: transferencia concurrente de " << total
              << " bytes, wrap-around y cancelación de productor bloqueado\n";
    return 0;
}
