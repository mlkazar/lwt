#ifndef __THREAD_MUTEX_H_ENV__
#define __THREAD_MUTEX_H_ENV__ 1

#include "thread.h"

class ThreadCond;
class ThreadMutex;

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

 private:
    SpinLock _lock;
    dqueue<Thread> _waiting;
    Thread *_ownerp;

    /* the releaseNL call is made while holding _lock, and releases the mutex, and finally
     * also releases the internal spin lock.  So, this call is just like release except
     * the spin lock is held on entry, but left released on exit.
     */
    void releaseAndSleep(Thread *threadp);

 public:
    void take();

    int tryLock();

    void release();
};

#endif /* __THREAD_MUTEX_H_ENV__ */

