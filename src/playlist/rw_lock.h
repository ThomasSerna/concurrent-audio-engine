//Include guard for reading the file only once
#pragma once

//Include of required files
#include <pthread.h>

//Include of required tools
#include <cassert>
#include <system_error>

//Wrapper for encapsulating the files
//contained in this folder under the 
//name playlist
namespace playlist {

//This function takes care of making 
//sure that multiple threads can read 
//the same element but not multiple
//threads can alterate the same file
//only one
class RwLock {
public:
    //Creates the options for locking
    //a file being read already
    //and indicating if there is an error
    RwLock() {
        pthread_rwlockattr_t attr;
        throw_if_error(pthread_rwlockattr_init(&attr), "pthread_rwlockattr_init");
//This will take care of giving writing 
//access to whichever thread requires it
//eviting in this way an undefined time
//for writing
#if defined(__GLIBC__)
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
        
#endif
        //Creates the lock for the file according to
        //what was requested and destroys the old
        //lock as it is not needed and then verifies
        //if there was an error
        const int rc = pthread_rwlock_init(&lock_, &attr);
        pthread_rwlockattr_destroy(&attr);
        throw_if_error(rc, "pthread_rwlock_init");
    }

    //When destroying the object destroys the lock
    //eviting in this way saturating the memory
    //with unrequired locks
    ~RwLock() { pthread_rwlock_destroy(&lock_); }

    //This evites copying a lock
    RwLock(const RwLock&) = delete;
    RwLock& operator=(const RwLock&) = delete;
    RwLock(RwLock&&) = delete;
    RwLock& operator=(RwLock&&) = delete;

    //Requests the lock to allow writing and
    //indicates if it was a success or not
    //to continue fast
    void lock() { throw_if_error(pthread_rwlock_wrlock(&lock_), "pthread_rwlock_wrlock"); }
    bool try_lock() noexcept { return pthread_rwlock_trywrlock(&lock_) == 0; }

    //Requests the lock for reading.It
    //is shared as multiple threads can 
    //read a file and indicates if it was
    //a success or not to continue the 
    //proccess 
    void lock_shared() { throw_if_error(pthread_rwlock_rdlock(&lock_), "pthread_rwlock_rdlock"); }
    bool try_lock_shared() noexcept { return pthread_rwlock_tryrdlock(&lock_) == 0; }

    //Unlocks the lock for reading
    void unlock() noexcept { release(); }
    //unlocks the lock for writing
    void unlock_shared() noexcept { release(); }

    //Returns a modifiable lock pointer
    //for no constant objects used for writing files
    pthread_rwlock_t* native_handle() noexcept { return &lock_; }
    //Returns a not modifiable lock pointer
    //for constant objects, used for reading files
    const pthread_rwlock_t* native_handle() const noexcept { return &lock_; }

private:
    //Private function to throw errors
    //when handling the locks
    static void throw_if_error(int rc, const char* what) {
        if (rc != 0) {
            throw std::system_error(rc, std::generic_category(), what);
        }
    }

    //Private function to throw errors
    //when handling the release of the locks
    void release() noexcept {
        [[maybe_unused]] const int rc = pthread_rwlock_unlock(&lock_);
        assert(rc == 0);
    }

    //attribute of the lock 
    pthread_rwlock_t lock_;
};

}  