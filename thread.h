#ifndef __THREAD_H_ENV__
#define __THREAD_H_ENV__ 1

#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <atomic>

#include "dqueue.h"

/* does a pointer fit in an integer? */
#define THREAD_PTR_FITS_IN_INT    1
//#define THREAD_PTR_FITS_IN_INT    0

class Thread;
class ThreadEntry;
class ThreadDispatcher;

class SpinLock {
 public:
    std::atomic<long> _owningPid;

    SpinLock() {
        _owningPid = 0;
    }

    void take() {
        long exchangeValue;
        long newValue;

        while(1) {
            exchangeValue = 0;
            newValue = 1;
            if (_owningPid.compare_exchange_strong(exchangeValue, 
                                                   newValue,
                                                   std::memory_order_acquire)) {
                /* success */
                break;
            }
            else {
                continue;
            }
        }
    }

    /* return true if we get the lock */
    int tryLock() {
        long exchangeValue;
        long newValue;
        exchangeValue = 0;
        newValue = 1;
        if (_owningPid.compare_exchange_weak(exchangeValue, 1, std::memory_order_acquire)) {
            /* success */
            return 1;
        }
        else {
            return 0;
        }
    }

    void release() {
        _owningPid.store(0, std::memory_order_release);
    }
};

class ThreadEntry {
 public:
    ThreadEntry *_dqNextp;
    ThreadEntry *_dqPrevp;
    Thread *_threadp;       /* back ptr */
};

class Thread {
 public:
    typedef void (InitProc) (void *contextp, Thread *threadp);

    static dqueue<ThreadEntry> _allThreads;
    static SpinLock _globalThreadLock;

    /* for when thread is blocked */
    Thread *_dqNextp;
    Thread *_dqPrevp;

    ThreadEntry _allEntry;
    ucontext_t _ctx;
    int _goingToSleep;

    static void ctxStart(int p1, int p2);

 public:
    Thread() {
        _goingToSleep = 0;
        _globalThreadLock.take();
        _allThreads.append(&_allEntry);
        _globalThreadLock.release();

        init();
    }

    virtual void start() = 0;

    void init();

    void resume();

    void queue();
};

/* this thread provides a context for running the dispatcher, so that when a thread
 * blocks, we can run the dispatcher without staying on the same stack.
 */
class ThreadIdle : public Thread {
 public:
    SpinLock *_userLockToReleasep;
    ThreadDispatcher *_disp;

    void start();

    ThreadIdle() {
        _userLockToReleasep = NULL;
    }

    void setLock(SpinLock *lockp) {
        _userLockToReleasep = lockp;
    }

    SpinLock *getLockAndClear() {
        SpinLock *lockp = _userLockToReleasep;
        _userLockToReleasep = NULL;
        return lockp;
    }
};

class ThreadDispatcher
{
 public:
    static pthread_once_t _once;
    static pthread_key_t _dispatcherKey;
    static const long _maxDispatchers=8;

    static ThreadDispatcher *_allDispatchers[_maxDispatchers];
    static uint16_t _dispatcherCount;

    dqueue<Thread> _runQueue;
    SpinLock _runQueueLock;

    Thread *_currentThreadp;
    int _sleeping;
    pthread_cond_t _runCV;
    pthread_mutex_t _runMutex;

    /* an idle thread that provides a thread with a stack on which we can run
     * the dispatcher.
     */
    ThreadIdle _idle;

    static void globalInit() {
        pthread_key_create(&_dispatcherKey, NULL);
    }

    static ThreadDispatcher *currentDispatcher();

    static void *dispatcherTop(void *ctx);

 public:
    /* called to put thread to sleep on current dispatcher, and then dispatch
     * more threads.
     */
    static void sleep(SpinLock *lockp);

    /* queue this thread on this dispatcher */
    void queueThread(Thread *threadp);

    /* called to look for work in the run queue, or wait until some shows up */
    void dispatch();

    /* called to create a bunch of dispatchers and their pthreads */
    static void setup(uint16_t ndispatchers);

    ThreadDispatcher();
};

#endif /* __THREAD_H_ENV__ */ 
