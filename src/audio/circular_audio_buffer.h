//Include guard for reading file only once
#pragma once

//Include of required tools
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

//This class solves the issue of ffmpeg
//producing sound faster than ffplay can
//consume it so to solve this issue we use
//a point in middle so we can reduce the
//bottleneck using a system of head,indicating
//where it is being read, and a tail,indicating
//wher it is being written down. Also using
//a used to count the bytes saved
class CircularAudioBuffer {
public:
    //Creates the memory space of maximum 256kb with a minimum
    //capacity of 1 as having a 0 space doesn't make sense
    explicit CircularAudioBuffer(std::size_t capacity = 256 * 1024)
        : storage_(std::max<std::size_t>(capacity, 1)) {}

    //Evites copying the object of the circularAudioBuffer
    //to evite issues with the integrity of objects being used
    CircularAudioBuffer(const CircularAudioBuffer&) = delete;
    CircularAudioBuffer& operator=(const CircularAudioBuffer&) = delete;

    //Takes care of cleaning the buffer and leaving it free
    //to whoever wants to use it by taking the lock which
    //in this case is mutex_ and dropping it after the space
    //is used and then tells to everyone that wants to read
    //or write that that space is free to use
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_ = used_ = 0;
        closed_ = false;
        readable_.notify_all();
        writable_.notify_all();
    }

    //Writes all of the bytes that is requested unless close()
    //is activated
    bool write(const char* source, std::size_t count) {
        std::size_t offset = 0;
        std::unique_lock<std::mutex> lock(mutex_);
        while (offset < count) {
            //Sleeps the write function if it is not
            //being used or there is not buffer space
            writable_.wait(lock, [&] { return closed_ || used_ < storage_.size(); });

            //Evites keeping the cycle if theres no space
            if (closed_) return false;

            //Calculates what is left to write, the free space of the buffer
            //and how much is left until the other process is finished to
            //continue
            const std::size_t amount = std::min({count - offset, storage_.size() - used_,
                                                 storage_.size() - tail_});
            //Moves the head and the tail to continue the circular process
            //of the audio buffer
            std::memcpy(storage_.data() + tail_, source + offset, amount);
            tail_ = (tail_ + amount) % storage_.size();
            used_ += amount;
            offset += amount;
            readable_.notify_one();
        }
        return true;
    }

   //Does the same process as write but focused on reading
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

    //Used for indicating availability in the buffer
    //and eviting that other processes dont happen
    //when another proccess finishes already
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        readable_.notify_all();
        writable_.notify_all();
    }

    //Indicates the size of the files to read or write
    //in a certain part of the process
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return used_;
    }

    //Indicates the real size of the file to write
    std::size_t capacity() const noexcept { return storage_.size(); }

private:
    //Atributes of the class
    std::vector<char> storage_;
    mutable std::mutex mutex_;
    std::condition_variable readable_;
    std::condition_variable writable_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t used_ = 0;
    bool closed_ = false;
};
