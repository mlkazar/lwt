/*

Copyright 2016-2020 Cazamar Systems

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */


#ifndef __THREADTIMER_H_ENV__
#define __THREADTIMER_H_ENV__ 1

#include <sys/types.h>
#include <pthread.h>
#include "thread.h"
#include "threadmutex.h"
#include "dqueue.h"

class ThreadTimer;

/* helper class for simple sleep operations */
class ThreadTimerSleep {
    uint32_t _ms;
    ThreadMutex _mutex;
    ThreadCond _cv;

    static void condWakeup(ThreadTimer *timerp, void *contextp);

 public:
    ThreadTimerSleep() {
        _cv.setMutex(&_mutex);
    }

    int32_t sleep(uint32_t ms);
};

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

    /* called with global lock held */
    void release() {
        assert(_refCount > 0);
        if (--_refCount == 0) {
            /* make sure it isn't in a queue */
            if (_inQueue) {
                _allTimers.remove(this);
                _inQueue = 0;
            }

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

    static int32_t sleep(uint32_t ams) {
        ThreadTimerSleep sleeper;
        int32_t code;

        code = sleeper.sleep(ams);
        return code;
    }

    static void init();
};

/* the reason for all this complexity is that we can't recognize a canceled timer
 * without using a static lock, since the context may be freed by the time the
 * timer fires.
 */
class ThreadCondTimed {
    static ThreadMutex _internalLock;
    ThreadTimer *_timerp;
    ThreadCond _cv;
    ThreadMutex *_mutexp;

    static void timerFired(ThreadTimer *timerp, void *contextp) {
        ThreadCondTimed *p = (ThreadCondTimed *) contextp;
        _internalLock.take();
        if (timerp->isCanceled()) {
            _internalLock.release();
            return;
        }
        p->_cv.broadcast();
        p->_timerp = NULL;
        _internalLock.release();
    }

 public:
    void setMutex(ThreadMutex *mutexp) {
        _mutexp = mutexp;
    }

    ThreadCondTimed() {
        _cv.setMutex(&_internalLock);
        _mutexp = NULL;
        _timerp = NULL;
    }

    ThreadCondTimed(ThreadMutex *mutexp) {
        _mutexp = mutexp;
        _timerp = NULL;
    }

    void broadcast() {
        _internalLock.take();
        _cv.broadcast();
        _internalLock.release();
    }

    void wait() {
        _internalLock.take();
        _mutexp->release();
        _cv.wait();
        _internalLock.release();
        _mutexp->take();
    }

    /* returns true if timer didn't fire, false if it did.  Note that timer firing
     * still might mean we were woken up by a broadcast, but if timer didn't fire,
     * we were definitely woken by a broadcast.  The return value really shouldn't be
     * used, except by code coverage tests.
     */
    int timedWait(uint32_t ms) {
        int rval;
        _internalLock.take();
        _mutexp->release();
        _timerp = new ThreadTimer(ms, &ThreadCondTimed::timerFired, this);
        _timerp->start();
        _cv.wait();
        if (_timerp) {
            _timerp->cancel();
            _timerp = NULL;
            rval = 1;
        }
        else 
            rval = 0;
        _internalLock.release();
        _mutexp->take();
        return rval;
    }
};

#endif /* __THREADTIMER_H_ENV__ */
