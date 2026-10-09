#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

class CircularAudioBuffer {
public:
    explicit CircularAudioBuffer(std::size_t capacity = 256 * 1024)
        : storage_(std::max<std::size_t>(capacity, 1)) {}

    CircularAudioBuffer(const CircularAudioBuffer&) = delete;
    CircularAudioBuffer& operator=(const CircularAudioBuffer&) = delete;

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_ = used_ = 0;
        closed_ = false;
        readable_.notify_all();
        writable_.notify_all();
    }

    // Escribe todos los bytes salvo que close() cancele la operación.
    bool write(const char* source, std::size_t count) {
        std::size_t offset = 0;
        std::unique_lock<std::mutex> lock(mutex_);
        while (offset < count) {
            writable_.wait(lock, [&] { return closed_ || used_ < storage_.size(); });
            if (closed_) return false;
            const std::size_t amount = std::min({count - offset, storage_.size() - used_,
                                                 storage_.size() - tail_});
            std::memcpy(storage_.data() + tail_, source + offset, amount);
            tail_ = (tail_ + amount) % storage_.size();
            used_ += amount;
            offset += amount;
            readable_.notify_one();
        }
        return true;
    }

    // Lee hasta capacity bytes. Devuelve 0 solo cuando se cerró y ya no quedan datos.
    std::size_t read(char* destination, std::size_t capacity) {
        std::unique_lock<std::mutex> lock(mutex_);
        readable_.wait(lock, [&] { return closed_ || used_ > 0; });
        if (used_ == 0) return 0;
        const std::size_t amount = std::min({capacity, used_, storage_.size() - head_});
        std::memcpy(destination, storage_.data() + head_, amount);
        head_ = (head_ + amount) % storage_.size();
        used_ -= amount;
        writable_.notify_one();
        return amount;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        readable_.notify_all();
        writable_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return used_;
    }

    std::size_t capacity() const noexcept { return storage_.size(); }

private:
    std::vector<char> storage_;
    mutable std::mutex mutex_;
    std::condition_variable readable_;
    std::condition_variable writable_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t used_ = 0;
    bool closed_ = false;
};
