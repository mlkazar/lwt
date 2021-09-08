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

/* general lock-like class for both read/write locks and mutexes, so that we can
 * have a single condition variable class that can release either type of lock
 * atomically with going to sleep.
 *
 * In practice, either type of lock will have to be implemented in terms of
 * spin locks to make this really work when the cond variable goes to sleep.
 */
class ThreadBaseLock {
 public:
    long long _waitUs;
    SpinLock _lock;

    virtual void take() = 0;

    virtual void release() = 0;

    virtual int tryLock() = 0;

    virtual void releaseAndSleep(Thread *threadp) = 0;

    virtual long long getWaitUs() {
        return _waitUs;
    }

    ThreadBaseLock() {
        _waitUs = 0;
    }

    virtual ~ThreadBaseLock() {
        return;
    }
};

/* condition variable.  The internals of all condition variables are protected by the
 * spin lock within the corresponding mutex.
 */
class ThreadCond {
    friend class ThreadBaseLock;

 private:
    dqueue<Thread> _waiting;
    ThreadBaseLock *_baseLockp;

 public:

    ThreadCond() {
        _baseLockp = NULL;
    }

    ThreadCond(ThreadBaseLock *baseLockp) {
        _baseLockp = baseLockp;
    }

    void wait(ThreadBaseLock *baseLockp = 0);

    void signal();

    void broadcast();

    void setMutex(ThreadBaseLock *baseLockp) {
        _baseLockp = baseLockp;
    }
};

class ThreadMutex : public ThreadBaseLock {
    friend class ThreadCond;
    friend class ThreadMutexDetect;

 private:
    dqueue<Thread> _waiting;
    Thread *_ownerp;

    /* the releaseNL call is made while holding _lock, and releases the mutex, and finally
     * also releases the internal spin lock.  So, this call is just like release except
     * the spin lock is held on entry, but left released on exit.
     */
    void releaseAndSleep(Thread *threadp);

 public:

    ThreadMutex() {
        _ownerp = NULL;
    }
    
    void take();

    int tryLock();

    void release();

    virtual ~ThreadMutex() {
        return;
    }

    static void checkForDeadlocks();
};

class ThreadLockTracker;

/* typically optional structure for tracking owners for read shared
 * locks.  Note that we don't record write locks in the tracker
 * structure so that we can use the ThreadBaseLock operations, which
 * don't have associated tracker objects, on them.  Since upgrade and
 * write locks are held exclusively, we can use _ownerp to find the
 * current holder.  The trackers will only refer to readers.
 */
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
    LockMode _lockMode;

    ThreadLockTracker() {
        _lockMode = _lockNone;
        _threadp = NULL;
    }
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
class ThreadLockRw : public ThreadBaseLock {
 private:
    /* the WaitReason values are stored in the thread's _sleepContext field */
    enum WaitReason {
        _reasonNone = 0,
        _reasonRead = 1,        /* waiting to get a read lock */
        _reasonWrite = 2,       /* waiting to get a write lock */
        _reasonUpgrade = 3,     /* waiting to get an upgrade lock */
        _reasonUpgradeToWrite = 4}; /* have upgrade lock, waiting to upgrade it to write */

    uint32_t _readCount;
    uint32_t _lockClock;        /* opaque for use by lock package */
    uint8_t _writeCount;        /* always 0 or 1 */
    uint8_t _upgradeCount;      /* always 0 or 1 */
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

    /* release a write lock and go to sleep atomically */
    void releaseAndSleep(Thread *threadp);

    void writeToRead();

 public:

    ThreadLockRw() {
        _ownerp = NULL;
        _readCount = 0;
        _writeCount = 0;
        _upgradeCount = 0;
        _upgradeToWrite = 0;
        _lockClock = 0;
        _ownerp = NULL;
    }
    
    void lockWrite(ThreadLockTracker *trackerp=0);

    int tryWrite(ThreadLockTracker *trackerp = 0);

    void releaseWrite(ThreadLockTracker *trackerp = 0);

    void lockRead(ThreadLockTracker *trackerp=0);

    int tryRead(ThreadLockTracker *trackerp=0);

    void releaseRead(ThreadLockTracker *trackerp=0);

    void lockUpgrade(ThreadLockTracker *trackerp=0);

    void upgradeToWrite();

    void releaseUpgrade(ThreadLockTracker *trackerp = 0);

    int readUnfair(int waitingLock);

    int upgradeUnfair();

    int writeUnfair();

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

    /* define base operations so condition variables will work with
     * these, as long as we only block for a CV while holding a write
     * lock.
     */
    void take() {
        lockWrite();
    }

    int tryLock() {
        return tryWrite();
    }

    void release() {
        releaseWrite(NULL);
    }

    void wakeNext();

    virtual ~ThreadLockRw() {
        return;
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
