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

    void wait(ThreadMutex *mutexp = 0);

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

/* typically optional structure for tracking owners for read-like shared locks */
class ThreadLockOwner {
    Thread *_threadp;
    ThreadLockOwner *_dqNextp;
    ThreadLockOwner *_dqPrevp;
    uint32_t _lockMode; /* 1 is write, 0 is read, turn into enum if necessary */
};

class ThreadLockRw {
 private:
    SpinLock _lock;
    uint32_t _readCount;
    uint8_t _writeCount;        /* always 0 or 1 */
    uint8_t _fairnessState;
    dqueue<Thread> _readersWaiting;
    dqueue<Thread> _writersWaiting;

    /* optional -- you don't have to use the mode that saves this state, but you should */
    dqueue<ThreadLockOwner> _ownerQueue;

    Thread *_ownerp;

    long long _waitUs;

    /* the releaseNL call is made while holding _lock, and releases the mutex, and finally
     * also releases the internal spin lock.  So, this call is just like release except
     * the spin lock is held on entry, but left released on exit.
     */
    void releaseReadAndSleep(Thread *threadp, ThreadLockOwner *ownerp);

    void releaseWriteAndSleep(Thread *threadp, ThreadLockOwner *ownerp);

 public:

    ThreadLockRw() {
        _ownerp = NULL;
        _waitUs = 0;
        _fairnessState = 0;
        _readCount = 0;
        _writeCount = 0;
        _ownerp = NULL;
        _waitUs = 0;
        
    }
    
    void writeLock(ThreadLockOwner *ownerp=0);

    int tryWrite(ThreadLockOwner *ownerp = 0);

    void releaseWrite(ThreadLockOwner *ownerp);

    void readLock(ThreadLockOwner *ownerp=0);

    int tryRead(ThreadLockOwner *ownerp=0);

    void releaseRead(ThreadLockOwner *ownerp=0);

    long long getWaitUs() {
        return _waitUs;
    }
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

    int sweepFrom(Thread *threadp, uint32_t sweepIx);
    
    int checkForDeadlocks();

    static void *mutexMonitorTop(void *cxp);

    static void start();
};

#endif /* __THREAD_MUTEX_H_ENV__ */
