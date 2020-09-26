#ifndef __THREADTIMER_H_ENV__
#define __THREADTIMER_H_ENV__ 1

#include <sys/types.h>
#include <pthread.h>
#include "thread.h"
#include "dqueue.h"

/* The model for ThreadTimers is a little subtle.  Because we're on an MP, there's
 * an inherent race between canceling a timer and the callback from a timer.  If a timer
 * fires, it will be automatically canceled when you return.  But also, the owner of a
 * timer can cancel it at any time, including a nanosecond before the callback begins
 * execution, when it is too late for the timer package to stop the callback.
 *
 * Thus, any timer callback needs to start with a call to _isDeleted, while holding a
 * lock that protects the pointer to the user's ThreadTimer.  If isDeleted says
 * that the timer has been canceled, then the callback must return immediately,
 * without accessing any thing through the context pointer.  That's because the
 * code that canceled the timer may have also deleted any structures holding the
 * timer.
 */
class ThreadTimer {
    /* statics used by the threadtimer server */
    static pthread_mutex_t _timerMutex;
    static pthread_cond_t _timerCond;
    static int _noticeFds[2];
    static int _threadRunning;
    static void *timerManager(void *contextp);
    static dqueue<ThreadTimer> _allTimers;
    static int _didInit;

 public:
    typedef void Callback(ThreadTimer *timerp, void *contextp);

 protected:
    /* stuff present in all of the timer instances */
    uint32_t _refCount;
    uint8_t _canceled;
    uint8_t _inQueue;
    uint32_t _msecs;
    uint64_t _expiration;

 public:
    ThreadTimer *_dqNextp;
    ThreadTimer *_dqPrevp;

    Callback *_callbackp;
    void *_contextp;

 public:
    ThreadTimer(uint32_t ms, Callback *procp, void *contextp) {
        _refCount = 1;
        _canceled = 0;
        _inQueue = 0;
        _msecs = ms;
        _callbackp = procp;
        _contextp = contextp;
    }

    void start();

    void hold() {
        _refCount++;
    }

    void release() {
        assert(_refCount > 0);
        if (--_refCount == 0) {
            assert(_canceled);
            delete this;
        }
    }


    /* returns true if canceled, false if timer is already going to run; frees the timer
     * structure (eventually, if it is already running).
     */
    int32_t cancel();

    int isCanceled() {
        return _canceled;
    }

    static void init();
};

#endif /* __THREADTIMER_H_ENV__ */