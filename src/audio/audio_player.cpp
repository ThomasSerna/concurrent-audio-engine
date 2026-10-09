#include "audio_player.h"

#if defined(_WIN32)
#include <cstdlib>
#include <io.h>
#include <string>
#include <vector>

namespace {

std::wstring to_wide_string(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring wide(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
    return wide;
}

bool file_exists_and_readable(const std::string& path) {
    const auto wide = to_wide_string(path);
    return wide.empty() ? false : (_waccess(wide.c_str(), 4) == 0);
}

} // namespace

void AudioPlayer::refresh() {
    if (pid <= 0 || process_handle == nullptr) {
        return;
    }

    const DWORD result = WaitForSingleObject(process_handle, 0);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(process_handle);
        process_handle = nullptr;
        pid = -1;
        state = PlayerState::STOPPED;
    }
}

bool AudioPlayer::play(const std::string& path) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (!file_exists_and_readable(path)) {
        return false;
    }

    if (pid > 0) {
        stop();
    }

    std::string command = "\"ffplay\" -nodisp -autoexit -loglevel error \"" + path + "\"";
    std::vector<char> buffer(command.begin(), command.end());
    buffer.push_back('\0');

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    if (!CreateProcessA(
            nullptr,
            buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        return false;
    }

    CloseHandle(process_info.hThread);
    process_handle = process_info.hProcess;
    pid = process_info.dwProcessId;
    state = PlayerState::PLAYING;
    return true;
}

bool AudioPlayer::stop() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (pid <= 0 || process_handle == nullptr) {
        return false;
    }

    if (state == PlayerState::PAUSED) {
        ResumeThread(process_handle);
    }

    if (TerminateProcess(process_handle, 1)) {
        WaitForSingleObject(process_handle, INFINITE);
    }

    CloseHandle(process_handle);
    process_handle = nullptr;
    pid = -1;
    state = PlayerState::STOPPED;
    return true;
}

bool AudioPlayer::resume() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (state != PlayerState::PAUSED || pid <= 0 || process_handle == nullptr) {
        return false;
    }

    if (ResumeThread(process_handle) == static_cast<DWORD>(-1)) {
        return false;
    }

    state = PlayerState::PLAYING;
    return true;
}

bool AudioPlayer::pause() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (state != PlayerState::PLAYING || pid <= 0 || process_handle == nullptr) {
        return false;
    }

    if (SuspendThread(process_handle) == static_cast<DWORD>(-1)) {
        return false;
    }

    state = PlayerState::PAUSED;
    return true;
}

bool AudioPlayer::close() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();

    if (pid <= 0 || process_handle == nullptr) {
        return true;
    }

    if (TerminateProcess(process_handle, 1)) {
        WaitForSingleObject(process_handle, INFINITE);
    }

    CloseHandle(process_handle);
    process_handle = nullptr;
    pid = -1;
    state = PlayerState::STOPPED;
    return true;
}

PlayerState AudioPlayer::getState() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();
    return state;
}

bool AudioPlayer::prefetch(const std::string&) {
    return false;  // la precarga solo existe en la ruta POSIX
}

#else

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

bool wait_for_child(ProcessId child) {
    if (child <= 0) return true;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        if (errno == ECHILD) return true;
        return false;
    }
    return true;
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

} // namespace

void AudioPlayer::refresh() {
    if (pid > 0) {
        int status = 0;
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) pid = -1;
    }
    if (output_pid_ > 0) {
        int status = 0;
        const pid_t result = waitpid(output_pid_, &status, WNOHANG);
        if (result == output_pid_ || (result < 0 && errno == ECHILD)) {
            output_pid_ = -1;
            // ffplay termino: el audio ya se reprodujo por completo.
            state.store(PlayerState::STOPPED, std::memory_order_release);
        }
    }
}

ProcessId AudioPlayer::start_decoder(const std::string& path, int& read_fd) {
    // O_CLOEXEC: cada hijo hereda solo lo necesario. Sin esto, el ffmpeg de la
    // precarga heredaba el extremo lector del pipe de la pista actual y impedia
    // que el decodificador anterior recibiera EPIPE, bloqueando la parada.
    int pipe_fds[2] = {-1, -1};
    if (pipe2(pipe_fds, O_CLOEXEC) != 0) return -1;
    const pid_t child = fork();
    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        execlp("ffmpeg", "ffmpeg", "-nostdin", "-v", "error", "-i", path.c_str(),
               "-f", "s16le", "-acodec", "pcm_s16le", "-ar", "44100", "-ac", "2",
               "pipe:1", static_cast<char*>(nullptr));
        _exit(127);
    }
    ::close(pipe_fds[1]);
    if (child < 0) {
        ::close(pipe_fds[0]);
        return -1;
    }
    read_fd = pipe_fds[0];
    return child;
}

void AudioPlayer::drop_prefetch_locked() {
    if (prefetch_pid_ > 0) {
        kill(prefetch_pid_, SIGTERM);  // ffmpeg puede estar bloqueado escribiendo en su pipe
    }
    close_fd(prefetch_fd_);
    if (prefetch_pid_ > 0) {
        wait_for_child(prefetch_pid_);
    }
    prefetch_pid_ = 0;
    prefetch_path_.clear();
}

bool AudioPlayer::prefetch(const std::string& path) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    if (prefetch_fd_ >= 0 && prefetch_path_ == path) return true;  // ya esta lista
    drop_prefetch_locked();
    if (access(path.c_str(), R_OK) != 0) return false;

    int read_fd = -1;
    const ProcessId child = start_decoder(path, read_fd);
    if (child < 0) return false;
    prefetch_pid_ = child;
    prefetch_fd_ = read_fd;
    prefetch_path_ = path;
    return true;
}

bool AudioPlayer::play(const std::string& path) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    stop_locked();  // solo para la pista anterior; el prefetch no se toca aqui

    if (access(path.c_str(), R_OK) != 0) return false;

    int decoded_fd = -1;
    ProcessId decoder = -1;
    if (prefetch_fd_ >= 0 && prefetch_path_ == path) {
        // La pista ya estaba decodificandose: se reutiliza su decodificador.
        decoded_fd = prefetch_fd_;
        decoder = prefetch_pid_;
        prefetch_fd_ = -1;
        prefetch_pid_ = 0;
        prefetch_path_.clear();
    } else {
        drop_prefetch_locked();  // el prefetch era de otra pista: ya no sirve
        decoder = start_decoder(path, decoded_fd);
        if (decoder < 0) return false;
    }

    // Pipe hacia ffplay (salida de audio).
    int output_pipe[2] = {-1, -1};
    if (pipe2(output_pipe, O_CLOEXEC) != 0) {
        kill(decoder, SIGTERM);
        ::close(decoded_fd);
        wait_for_child(decoder);
        return false;
    }

    const pid_t output = fork();
    if (output == 0) {
        dup2(output_pipe[0], STDIN_FILENO);
        ::close(decoded_fd);
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        execlp("ffplay", "ffplay", "-nodisp", "-autoexit", "-loglevel", "error",
               "-f", "s16le", "-ar", "44100", "-ch_layout", "stereo", "-i", "pipe:0",
               static_cast<char*>(nullptr));
        _exit(127);
    }
    if (output < 0) {
        kill(decoder, SIGTERM);
        ::close(decoded_fd);
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        wait_for_child(decoder);
        return false;
    }

    ::close(output_pipe[0]);
    decoded_fd_ = decoded_fd;
    output_fd_ = output_pipe[1];
    pid = decoder;
    output_pid_ = output;
    stopping_.store(false, std::memory_order_release);
    buffer_.reset();

    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    sigaction(SIGPIPE, &action, nullptr);

    state.store(PlayerState::PLAYING, std::memory_order_release);
    try {
        producer_thread_ = std::thread(&AudioPlayer::produce_pcm, this);
        consumer_thread_ = std::thread(&AudioPlayer::consume_pcm, this);
    } catch (...) {
        stop_locked();
        return false;
    }
    return true;
}

void AudioPlayer::produce_pcm() {
    std::vector<char> chunk(16 * 1024);
    for (;;) {
        const ssize_t count = ::read(decoded_fd_, chunk.data(), chunk.size());
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            if (stopping_.load(std::memory_order_acquire)) break;
            break;
        }
        if (!buffer_.write(chunk.data(), static_cast<std::size_t>(count))) break;
    }
    close_fd(decoded_fd_);
    buffer_.close(); // despierta al consumidor, que vacía lo que quede en el búfer
}

void AudioPlayer::consume_pcm() {
    std::vector<char> chunk(16 * 1024);
    for (;;) {
        const std::size_t count = buffer_.read(chunk.data(), chunk.size());
        if (count == 0) break;
        std::size_t offset = 0;
        while (offset < count) {
            const ssize_t written = ::write(output_fd_, chunk.data() + offset, count - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                buffer_.close();
                break;
            }
        }
        if (offset < count) break;
    }
    // Aqui NO se marca STOPPED: el fin real lo detecta refresh() cuando ffplay
    // sale, para no cortar el final de la pista (ffplay aun tiene audio en curso).
    close_fd(output_fd_);
}

void AudioPlayer::stop_locked() {
    stopping_.store(true, std::memory_order_release);
    buffer_.close();

    // Un proceso detenido con SIGSTOP no procesa SIGTERM hasta reanudarse:
    // sin este SIGCONT, join() de los hilos se bloquea para siempre.
    if (pid > 0) kill(pid, SIGCONT);
    if (output_pid_ > 0) kill(output_pid_, SIGCONT);

    if (pid > 0) kill(pid, SIGTERM);
    if (output_pid_ > 0) kill(output_pid_, SIGTERM);

    if (producer_thread_.joinable()) producer_thread_.join();
    if (consumer_thread_.joinable()) consumer_thread_.join();

    close_fd(decoded_fd_);
    close_fd(output_fd_);
    if (pid > 0) {
        wait_for_child(pid);
        pid = -1;
    }
    if (output_pid_ > 0) {
        wait_for_child(output_pid_);
        output_pid_ = -1;
    }
    state.store(PlayerState::STOPPED, std::memory_order_release);
}

bool AudioPlayer::stop() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    const bool was_active = state.load(std::memory_order_acquire) != PlayerState::STOPPED ||
                            pid > 0 || output_pid_ > 0;
    stop_locked();
    drop_prefetch_locked();
    return was_active;
}

bool AudioPlayer::pause() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();
    if (state.load(std::memory_order_acquire) != PlayerState::PLAYING || output_pid_ <= 0)
        return false;
    // El decodificador puede haber terminado antes que el audio almacenado en el búfer.
    if (pid > 0 && kill(pid, SIGSTOP) != 0) return false;
    if (kill(output_pid_, SIGSTOP) != 0) {
        if (pid > 0) kill(pid, SIGCONT);
        return false;
    }
    state.store(PlayerState::PAUSED, std::memory_order_release);
    return true;
}

bool AudioPlayer::resume() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();
    if (state.load(std::memory_order_acquire) != PlayerState::PAUSED || output_pid_ <= 0)
        return false;
    if (pid > 0 && kill(pid, SIGCONT) != 0) return false;
    if (kill(output_pid_, SIGCONT) != 0) {
        if (pid > 0) kill(pid, SIGSTOP);
        return false;
    }
    state.store(PlayerState::PLAYING, std::memory_order_release);
    return true;
}

bool AudioPlayer::close() { return stop(); }

PlayerState AudioPlayer::getState() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    refresh();
    return state.load(std::memory_order_acquire);
}

#endif

AudioPlayer::~AudioPlayer() { close(); }
