#pragma once

#include <pthread.h>

#include <cassert>
#include <system_error>

namespace playlist {

// Envoltorio RAII sobre pthread_rwlock_t.
//
// Cumple los requisitos de SharedMutex (lock/unlock/lock_shared/unlock_shared),
// por lo que se usa con guardas estándar:
//     std::shared_lock guard(lock);   // lectores (concurrentes entre sí)
//     std::unique_lock guard(lock);   // escritor (exclusivo)
//
// Notas de diseño:
//  * glibc da prioridad a los lectores por defecto, lo que permite que un flujo
//    continuo de lecturas deje sin servicio a un escritor (starvation). Se
//    configura prioridad al escritor para que "agregar/eliminar" no se retrase
//    mientras el motor de audio consulta la lista.
//  * Con prioridad al escritor, adquirir el lock de lectura de forma recursiva
//    en el mismo hilo puede producir interbloqueo. Por eso Playlist nunca llama
//    a un método público estando dentro de una sección crítica.
class RwLock {
public:
    RwLock() {
        pthread_rwlockattr_t attr;
        throw_if_error(pthread_rwlockattr_init(&attr), "pthread_rwlockattr_init");
#if defined(__GLIBC__)
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
        const int rc = pthread_rwlock_init(&lock_, &attr);
        pthread_rwlockattr_destroy(&attr);
        throw_if_error(rc, "pthread_rwlock_init");
    }

    ~RwLock() { pthread_rwlock_destroy(&lock_); }

    RwLock(const RwLock&) = delete;
    RwLock& operator=(const RwLock&) = delete;
    RwLock(RwLock&&) = delete;
    RwLock& operator=(RwLock&&) = delete;

    void lock() { throw_if_error(pthread_rwlock_wrlock(&lock_), "pthread_rwlock_wrlock"); }
    bool try_lock() noexcept { return pthread_rwlock_trywrlock(&lock_) == 0; }

    void lock_shared() { throw_if_error(pthread_rwlock_rdlock(&lock_), "pthread_rwlock_rdlock"); }
    bool try_lock_shared() noexcept { return pthread_rwlock_tryrdlock(&lock_) == 0; }

    void unlock() noexcept { release(); }
    void unlock_shared() noexcept { release(); }

    pthread_rwlock_t* native_handle() noexcept { return &lock_; }
    const pthread_rwlock_t* native_handle() const noexcept { return &lock_; }

private:
    static void throw_if_error(int rc, const char* what) {
        if (rc != 0) {
            throw std::system_error(rc, std::generic_category(), what);
        }
    }

    void release() noexcept {
        [[maybe_unused]] const int rc = pthread_rwlock_unlock(&lock_);
        assert(rc == 0);
    }

    pthread_rwlock_t lock_;
};

}  // namespace playlist