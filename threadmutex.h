#ifndef __THREAD_MUTEX_H_ENV__
#define __THREAD_MUTEX_H_ENV__ 1

#include "thread.h"

class ThreadCond;
class ThreadMutex;
class ThreadMutexDetect;

/* condition variable.  The internals of all condition variables are protected by the
 * spin lock within the corresponding mutex.
 */
class ThreadCond {
    friend class ThreadMutex;

 private:
    dqueue<Thread> _waiting;
    ThreadMutex *_mutexp;

 public:

    ThreadCond() {
        _mutexp = NULL;
    }

    ThreadCond(ThreadMutex *mutexp) {
        _mutexp = mutexp;
    }

    void wait(ThreadMutex *mutexp);

    void signal();

    void broadcast();

    void setMutex(ThreadMutex *mutexp) {
        _mutexp = mutexp;
    }
};

class ThreadMutex {
    friend class ThreadCond;
    friend class ThreadMutexDetect;

 private:
    SpinLock _lock;
    dqueue<Thread> _waiting;
    Thread *_ownerp;
    long long _waitUs;

    /* the releaseNL call is made while holding _lock, and releases the mutex, and finally
     * also releases the internal spin lock.  So, this call is just like release except
     * the spin lock is held on entry, but left released on exit.
     */
    void releaseAndSleep(Thread *threadp);

 public:

    ThreadMutex() {
        _ownerp = NULL;
        _waitUs = 0;
    }
    
    void take();

    int tryLock();

    void release();

    long long getWaitUs() {
        return _waitUs;
    }

    static void checkForDeadlocks();
};

class ThreadMutexDetect {
 public:
    static const uint32_t _maxCycleDepth = 1024;
    uint32_t _currentIx;
    Thread *_stack[_maxCycleDepth];
    
    ThreadMutexDetect() {
        _currentIx = 0;
    }

    void push(Thread *threadp) {
        if (_currentIx >= _maxCycleDepth)
            return;
        _stack[_currentIx++] = threadp;
    }

    void reset() {
        _currentIx = 0;
    }

    void displayTrace();

    int sweepFrom(Thread *threadp, int sweepIx);
    
    int checkForDeadlocks();

    static void *mutexMonitorTop(void *cxp);

    static void start();
};

#endif /* __THREAD_MUTEX_H_ENV__ */
