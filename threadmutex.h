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

class ThreadLockTracker;

/* typically optional structure for tracking owners for read-like shared locks */
class ThreadLockTracker {
 public:
    enum LockMode {
        _lockNone = 0,
        _lockRead = 1,
        _lockWrite = 2,
        _lockUpgrade = 3
    };
    Thread *_threadp;
    ThreadLockTracker *_dqNextp;
    ThreadLockTracker *_dqPrevp;
    LockMode _lockMode;         /* 1 is write, 0 is read, turn into enum if necessary */
};

/* Fair read write lock with upgrade potential.
 *
 * Readers increment readCount, writers increment writeCount, upgrade
 * holders increment upgradeCount.  When upgrading to write, set
 * upgradeToWrite and wait until readers goes to zero.  Note that you
 * can't (of course) upgrade to write before actually obtaining the
 * upgrade lock.
 *
 * When an upgrader is waiting to upgrade to write, they're in the
 * writesWaiting queue, the upgradeToWrite flag is set, and _ownerp
 * contains the thread that holds the upgrade/write lock.
 */
class ThreadLockRw {
 private:
    /* the WaitReason values are stored in the thread's _sleepContext field */
    enum WaitReason {
        _reasonNone = 0,
        _reasonRead = 1,        /* waiting to get a read lock */
        _reasonWrite = 2,       /* waiting to get a write lock */
        _reasonUpgrade = 3,     /* waiting to get an upgrade lock */
        _reasonUpgradeToWrite = 4}; /* have upgrade lock, waiting to upgrade it to write */

    SpinLock _lock;
    uint32_t _readCount;
    uint8_t _writeCount;        /* always 0 or 1 */
    uint8_t _upgradeCount;      /* always 0 or 1 */
    uint8_t _fairness;          /* true if last granted is write/upgradelock */
    uint8_t _upgradeToWrite;    /* true iff upgrade to write is pending */

    /* note that when a thread is waiting to upgrade to a write lock from an upgrade
     * lock, it is also in the writesWaiting queue, but the sleepContext field
     * in the waiting thread is set to _reasonUpgradeToWrite instead of
     * reasonWrite.
     */
    dqueue<Thread> _readsWaiting;        /* queue of threads waiting for read locks */
    dqueue<Thread> _writesWaiting;       /* queue of threads waiting for write locks */
    dqueue<Thread> _upgradesWaiting;     /* queue of threads waiting for upgrade */

    Thread *_ownerp;

 public:
    /* optional -- you don't have to use the mode that saves this state, but you should */
    dqueue<ThreadLockTracker> _trackerQueue;

    long long _waitUs;

    /* the releaseNL call is made while holding _lock, and releases the mutex, and finally
     * also releases the internal spin lock.  So, this call is just like release except
     * the spin lock is held on entry, but left released on exit.
     */
    void releaseReadAndSleep(Thread *threadp, ThreadLockTracker *trackerp);

    void releaseWriteAndSleep(Thread *threadp, ThreadLockTracker *trackerp);

 public:

    ThreadLockRw() {
        _ownerp = NULL;
        _waitUs = 0;
        _fairness = 0;
        _readCount = 0;
        _writeCount = 0;
        _upgradeCount = 0;
        _upgradeToWrite = 0;
        _ownerp = NULL;
        _waitUs = 0;
        
    }
    
    void lockWrite(ThreadLockTracker *trackerp=0);

    int tryWrite(ThreadLockTracker *trackerp = 0);

    void releaseWrite(ThreadLockTracker *trackerp);

    void lockRead(ThreadLockTracker *trackerp=0);

    int tryRead(ThreadLockTracker *trackerp=0);

    void releaseRead(ThreadLockTracker *trackerp=0);

    void lockUpgrade(ThreadLockTracker *trackerp=0);

    void upgradeToWrite();

    void releaseUpgrade(ThreadLockTracker *trackerp = 0);

    void lockMode(ThreadLockTracker::LockMode mode, ThreadLockTracker *trackerp = 0) {
        if (mode == ThreadLockTracker::_lockRead)
            lockRead(trackerp);
        else if (mode == ThreadLockTracker::_lockWrite)
            lockWrite(trackerp);
        else if (mode == ThreadLockTracker::_lockUpgrade)
            lockUpgrade(trackerp);
    }

    void releaseMode(ThreadLockTracker::LockMode mode, ThreadLockTracker *trackerp = 0) {
        if (mode == ThreadLockTracker::_lockRead)
            releaseRead(trackerp);
        else if (mode == ThreadLockTracker::_lockWrite)
            releaseWrite(trackerp);
        else if (mode == ThreadLockTracker::_lockUpgrade)
            releaseUpgrade(trackerp);
    }

    void wakeNext();

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
