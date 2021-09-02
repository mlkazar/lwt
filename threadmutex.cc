/*

Copyright 2016-2021 Cazamar Systems

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

#include "thread.h"
#include "threadmutex.h"
#include <assert.h>
#include <stdio.h>

/*****************ThreadMutex*****************/
void
ThreadMutex::take() {
    Thread *mep;
    long long blockedTime;

    mep = Thread::getCurrent();
    _lock.take();

    /* go in a loop waiting for a null owner, and then claim the lock;
     * note that when we're woken from the waiting queue, we must grab
     * the lock if it is available, since the release code will only wakeup
     * one sleeper at a time.
     */
    assert(_ownerp != mep);
    while(_ownerp != NULL) {
        mep->_blockingMutexp = this;
        blockedTime = osp_getUs();
        _waiting.append(mep);
        mep->sleep(&_lock);
        mep->_blockingMutexp = NULL;
        _lock.take();
        _waitUs += osp_getUs() - blockedTime;
    }
    _ownerp = mep;
    _lock.release();
}

/* return 1 if we get the lock, but never block */
int
ThreadMutex::tryLock() {
    Thread *mep;

    mep = Thread::getCurrent();
    _lock.take();

    /* go in a loop waiting for a null owner, and then claim the lock;
     * note that when we're woken from the waiting queue, we must grab
     * the lock if it is available, since the release code will only wakeup
     * one sleeper at a time.
     */
        
    assert(_ownerp != mep);
    if (_ownerp != NULL) {
        _lock.release();
        return 0;
    }
    else {
        _ownerp = mep;
        _lock.release();
        return 1;
    }
}

/* release a mutex */
void
ThreadMutex::release() {
    Thread *mep;
    Thread *nextp;

    mep = Thread::getCurrent();

    _lock.take();
    
    assert(_ownerp == mep);
    _ownerp = NULL;

    nextp = _waiting.pop();
    _lock.release();

    /* and now queue the task we've found in the waiting queue */
    if (nextp)
        nextp->queue();
}

/* Internal; release the lock owned by mep.  The mutex's spin lock
 * should be held, when we're called, and the spin lock will be
 * released before we're done.  This is primarily used by the
 * conditional variable code.
 */
void
ThreadMutex::releaseAndSleep(Thread *mep) {
    Thread *nextp;
    assert(_ownerp == mep);

    /* do the basics of the mutex release */
    _ownerp = NULL;
    nextp = _waiting.pop();

    if (nextp)
        nextp->queue();
    
    /* and go to sleep atomically */
    mep->sleep(&_lock);
}

/*****************TheadMutexDetect*****************/

/* check for deadlocks; note that we try to stop all dispatchers so
 * that mutexes aren't changed while we're checking for deadlocks.
 *
 * We're searching for lock cycles, but it is possible that some other
 * held mutexes are not part of a cycle.
 */
int
ThreadMutexDetect::checkForDeadlocks()
{
    Thread *threadp;
    ThreadEntry *ep;
    uint32_t sweepIx = 0;
    int didAny = 0;
    int allStopped;

    ThreadDispatcher::pauseAllDispatching();

    allStopped = ThreadDispatcher::pausedAllDispatching();

    if (allStopped) {
        for( ep = Thread::_allThreads.head(); ep; ep=ep->_dqNextp) {
            threadp = ep->_threadp;
            threadp->_marked = 0;
        }

        didAny = 0;
        for( ep = Thread::_allThreads.head(); ep; ep=ep->_dqNextp) {
            threadp = ep->_threadp;
            sweepIx++;
            reset();
            didAny = sweepFrom(threadp, sweepIx);
            if (didAny)
                break;
        }
    }
    else {
        didAny = 0;
    }

    ThreadDispatcher::resumeAllDispatching();

    return didAny;
}

/* internal function to sweep a thread, tagging it with sweepIx to see if we've 
 * already visited it.
 */
int
ThreadMutexDetect::sweepFrom(Thread *threadp, uint32_t sweepIx)
{
    ThreadMutex *mutexp;
    int rcode;

    push(threadp);

    if (threadp->_marked == sweepIx) {
        displayTrace();
        return 1;
    }

    threadp->_marked = sweepIx;

    mutexp = threadp->_blockingMutexp;
    if (mutexp == NULL) {
        return 0;
    }

    if (mutexp->_ownerp) {
        rcode = sweepFrom(mutexp->_ownerp, sweepIx);
        return rcode;
    }

    /* unlocked mutex, we're done */
    return 0;
}

/* internal function to display the thread trace when a deadlock has been detected */
void
ThreadMutexDetect::displayTrace()
{
    uint32_t i;
    Thread *threadp;

    printf("Deadlock detected:\n");
    for(i=0;i<_currentIx;i++) {
        threadp = _stack[i];
        printf("Deadlock thread %p waits for mutex=%p owned by thread %p\n",
               threadp, threadp->_blockingMutexp, threadp->_blockingMutexp->_ownerp);
    }
}

/* internal monitor thread */
/* static */ void *
ThreadMutexDetect::mutexMonitorTop(void *acxp)
{
    ThreadMutexDetect detect;
    int code;

    while(1) {
        sleep(10);
        code = detect.checkForDeadlocks();
        if (code) {
            printf("main: deadlocks found\n\n");
            assert("deadlocked" == 0);
        }
    }
}

/* start a monitoring thread watching for deadlocks */
/* static */ void
ThreadMutexDetect::start()
{
    pthread_t junk;
    pthread_create(&junk, NULL, mutexMonitorTop, NULL);
}

/*****************ThreadCond*****************/
void
ThreadCond::wait(ThreadBaseLock *baseLockp)
{
    Thread *mep = Thread::getCurrent();

    /* keep track which mutex is associated with this condition variable */
    if (_baseLockp == NULL)
        _baseLockp = baseLockp;
    else if (baseLockp == NULL) {
        baseLockp = _baseLockp;
    }
    else {
        assert(_baseLockp == baseLockp);
    }

    /* grab the spinlock protecting te mutex, and put ourselves to sleep
     * on the CV.  Finally, drop the mutex.
     */
    baseLockp->_lock.take();
    
    /* queue our task for the CV */
    _waiting.append(mep);

    /* and block, releasing the associated mutex */
    baseLockp->releaseAndSleep(mep);

    /* and reobtain it on the way back out */
    baseLockp->take();
}

/* wakeup a single waiting thread */
void
ThreadCond::signal()
{
    Thread *headp;

    _baseLockp->_lock.take();
    headp = _waiting.pop();
    _baseLockp->_lock.release();

    headp->queue();
}

/* wakeup all waiting threads */
void
ThreadCond::broadcast()
{
    Thread *headp;
    Thread *nextp;

    _baseLockp->_lock.take();
    headp = _waiting.head();
    _waiting.init();
    _baseLockp->_lock.release();

    /* and wakeup all, noting that as soon as a thread is queued, the rest of its
     * queue pointers may change.
     */
    for(; headp; headp=nextp) {
        nextp = headp->_dqNextp;
        headp->queue();
    }
}

/********************************ThreadLockRw********************************/

/* There are several things to note in this code.
 *
 * First, it tries to be fair enough that no starvation can occur.
 * Because readers can jump ahead of writers with a long enough queue
 * of readers, we have a flag _fairness that is set to zero when a
 * read or upgrade lock is granted, and which will prevent new read or
 * upgrade locks being granted until a write lock is granted, if one
 * is waiting.
 *
 * Second, threads are queued in separate queues for readers, writers
 * and upgraders, but the threads in the write queue might be waiting
 * for a write lock, or waiting to upgrade an upgradeable lock to a
 * write lock.  In the latter case, to grant it, we need to atomicslly
 * drop the upgrade lock and grant the write lock once the reader
 * count has dropped to zero.  To distinguish between these cases, we
 * use the thread's sleepContext field, storing the reason the thread
 * blocked in that field.
 */


/* The way these locks work, we've already been granted the lock by the time we wake up */
void
ThreadLockRw::lockWrite(ThreadLockTracker *trackerp)
{
    Thread *threadp = Thread::getCurrent();

    _lock.take();
    
    if (_writeCount + _upgradeCount + _readCount > 0) {
        _writesWaiting.append(threadp);
        threadp->_lockClock = _lockClock++;
        threadp->sleep(&_lock);
        _lock.take();
    }
    else {
        /* we can get the lock immediately, and w/o any fairness issues; we know
         * there are no fairness issues because if anyone was waiting for a lock,
         * someone would have to actually be holding the lock now, and no one is.
         */
        _ownerp = threadp;
        _writeCount++;
    }

    _lock.release();
}

/* return 1 if got the lock.  TryLock calls ignore fairness */
int
ThreadLockRw::tryWrite(ThreadLockTracker *trackerp)
{
    Thread *threadp = Thread::getCurrent();

    _lock.take();
    
    if (_writeCount + _upgradeCount + _readCount > 0) {
        _lock.release();
        /* failure case */
        return 0;
    }
    else {
        /* we can get the lock immediately, and w/o any fairness issues; we know
         * there are no fairness issues because if anyone was waiting for a lock,
         * someone would have to actually be holding the lock now, and no one is.
         */
         _ownerp = threadp;
         _writeCount++;
    }

    _lock.release();
    return 1;
}

/* must be called with the spin lock held, and after you've checked
 * that there's a read lock to try to grant.
 */
int
ThreadLockRw::readUnfair(Thread *grantThreadp)
{
    Thread *starveThreadp;
    uint32_t readLockClock;

    /* lock we want to grant */
    if (grantThreadp)
        readLockClock = grantThreadp->_lockClock;
    else
        readLockClock = _readsWaiting.head()->_lockClock;

    /* see if the guy we might be starving has been waiting too long, i.e.
     * more than threadClockWindow queues longer than the lock we're
     * thinking of granting.  If so, don't grant the reader.
     */
    if ((starveThreadp = _writesWaiting.head()) != NULL) {
        if ( threadClockCmp( starveThreadp->_lockClock,
                             readLockClock - threadClockReadWindow) < 0) {
            return 1;
        }
    }
    return 0;
}

/* must be called with the spin lock held; check to see if we'd be starving the
 * write lock by granting this upgrade lock.
 */
int
ThreadLockRw::upgradeUnfair()
{
    Thread *starveThreadp;
    Thread *grantLockp;

    /* lock we want to grant */
    grantLockp = _upgradesWaiting.head();

    /* see if the guy we might be starving has been waiting too long, i.e.
     * more than threadClockWindow queues longer than the lock we're
     * thinking of granting.  If so, don't grant the reader.
     */
    if ((starveThreadp = _writesWaiting.head()) != NULL) {
        if ( threadClockCmp( starveThreadp->_lockClock,
                             grantLockp->_lockClock - threadClockWriteWindow) < 0) {
            return 1;
        }
    }
    return 0;
}

int
ThreadLockRw::writeUnfair()
{
    Thread *starveThreadp;
    Thread *grantLockp;

    /* lock we want to grant */
    grantLockp = _writesWaiting.head();

    /* see if the guy we might be starving has been waiting too long, i.e.
     * more than threadClockWindow queues longer than the lock we're
     * thinking of granting.  If so, don't grant the reader.
     */
    if ((starveThreadp = _upgradesWaiting.head()) != NULL) {
        if ( threadClockCmp( starveThreadp->_lockClock,
                             grantLockp->_lockClock - threadClockWriteWindow) < 0) {
            return 1;
        }
    }
    return 0;
}

/* must be called with the internal _lock held for the read/write lock */
void
ThreadLockRw::wakeNext()
{
    Thread *grantThreadp;

    while ((grantThreadp = _readsWaiting.head()) != NULL) {
        if ( _writeCount == 0) {
            /* see if granting this read lock would be unfair */
            if ( readUnfair(NULL))
                break;

            /* we can grant this read lock to the thread */
            _readsWaiting.pop();
            _readCount++;
            grantThreadp->queue();
        } /* can grant a read lock */
        else {
            /* can't grant any more read locks */
            break;
        }
    } /* loop granting read locks while we can */

    if (_upgradeToWrite) {
        osp_assert(_ownerp != NULL && _writesWaiting.head() == _ownerp && _upgradeCount > 0);
        if (_readCount == 0) {
            _writesWaiting.remove(_ownerp);
            _upgradeCount = 0;
            _writeCount = 1;
            _upgradeToWrite = 0;
            _ownerp->queue();
        }

        /* if we're in the middle of an upgrade, we're not going to be
         * able to grant a write or upgrade lock anyway, and if we
         * just performed the upgrade, we have a write lock and can't
         * grant anything else, either.
         */
        return;
    }

    if ((grantThreadp = _upgradesWaiting.head()) != NULL) {
        /* we have an upgrade lock waiting */
        if ( _writeCount == 0 && _upgradeCount == 0) {
            if ( !upgradeUnfair()) {
                _upgradeCount = 1;
                _ownerp = grantThreadp;
                _upgradesWaiting.pop();
                grantThreadp->queue();
                return;
            }
        }
    }

    /* otherwise, try to grant a write lock */
    if ((grantThreadp = _writesWaiting.head()) != NULL) {
        if (_readCount + _writeCount + _upgradeCount == 0) {
            _writesWaiting.pop();
            _writeCount = 1;
            _ownerp = grantThreadp;
            grantThreadp->queue();
            return;
        }
    }
}

void
ThreadLockRw::releaseWrite(ThreadLockTracker *trackerp)
{
    Thread *threadp = Thread::getCurrent();

    _lock.take();
    assert(_writeCount > 0 && _ownerp == threadp);

    /* clear state indicating write locked */
    _writeCount--;
    _ownerp = NULL;

    wakeNext();
    _lock.release();
}

/* internal function: must be called with the base spin lock held.  Goes
 * to sleep and atomically drops the spin lock as well.
 */
void
ThreadLockRw::releaseAndSleep(Thread *threadp)
{
    assert(_writeCount > 0 && _ownerp == threadp);

    /* clear state indicating write locked */
    _writeCount--;
    _ownerp = NULL;

    wakeNext();

    threadp->sleep(&_lock);
}

void
ThreadLockRw::releaseUpgrade(ThreadLockTracker *trackerp)
{
    Thread *threadp = Thread::getCurrent();

    _lock.take();
    assert(_upgradeCount > 0 && _ownerp == threadp);

    /* clear state indicating write locked */
    _upgradeCount--;
    _ownerp = NULL;

    wakeNext();
    _lock.release();
}

void
ThreadLockRw::lockRead(ThreadLockTracker *trackerp)
{
    Thread *threadp;

    threadp = Thread::getCurrent();

    _lock.take();
    
    /* no writers waiting, we can add a read lock */
    if (_writeCount == 0) {
        if ( !readUnfair(threadp)) {
            if (trackerp) {
                trackerp->_lockMode = ThreadLockTracker::_lockRead;
                trackerp->_threadp = threadp;
                _trackerQueue.append(trackerp);
            }
            _readCount++;
            _lock.release();
            return;
        }
    }

    /* here, we queue the reader */
    threadp->_lockClock = _lockClock++;
    _readsWaiting.append(threadp);
    threadp->sleep(&_lock);

    if (trackerp) {
        _lock.take();
        trackerp->_lockMode = ThreadLockTracker::_lockRead;
        trackerp->_threadp = threadp;
        _trackerQueue.append(trackerp);
        _lock.release();
    }
    return;
}

/* return 1 if got locked */
int
ThreadLockRw::tryRead(ThreadLockTracker *trackerp)
{
    _lock.take();
    if (_writeCount == 0) {
        _readCount++;
        if (trackerp) {
            trackerp->_lockMode = ThreadLockTracker::_lockRead;
            trackerp->_threadp = Thread::getCurrent();
            _trackerQueue.append(trackerp);
        }
        _lock.release();
        return 1;
    }
    else {
        _lock.release();
        return 0;
    }
}

void
ThreadLockRw::releaseRead(ThreadLockTracker *trackerp)
{
    _lock.take();
    
    assert(_readCount > 0);
    _readCount--;
    if (trackerp) {
        trackerp->_lockMode = ThreadLockTracker::_lockNone;
        trackerp->_threadp = NULL;
        _trackerQueue.remove( trackerp);
    }

    wakeNext();

    _lock.release();
}

void
ThreadLockRw::writeToRead()
{
    Thread *threadp = Thread::getCurrent();

    _lock.take();
    assert(_writeCount > 0 && _ownerp == threadp);

    /* clear state indicating write locked */
    _writeCount--;
    _ownerp = NULL;

    /* and increment readers */
    _readCount++;
    
    _lock.release();
}

/* get an upgrade lock */
void
ThreadLockRw::lockUpgrade(ThreadLockTracker *trackerp)
{
    Thread *threadp;

    threadp = Thread::getCurrent();

    _lock.take();
    if (_ownerp == NULL) {
        /* we can get the lock */
        _ownerp = threadp;
        _upgradeCount++;
    }
    else {
        _upgradesWaiting.append(threadp);
        threadp->sleep(&_lock);
        _lock.take();
    }

    /* our rules for upgradeToWrite are that it is set to zero each
     * time a new thread bumps upgradeCount, before the lockUpgrade
     * call completes.  It is incremented on an upgradeToWrite call,
     * but cleared again by the time that call completes.
     */
    _upgradeToWrite = 0;

    _lock.release();
}

/* call to upgrade an upgrade lock to a write lock.  Key feature of this operation
 * is that no writers can sneak from the time we got the upgrade lock to the time
 * that we get the write lock.  Because we already hold the upgrade lock,
 * we know that there should be no writers and no other upgrade lock holders.
 * So, we just have to wait until the reader count goes to 0.
 */
void
ThreadLockRw::upgradeToWrite()
{
    Thread *threadp = Thread::getCurrent();

    _lock.take();
    
    assert(_upgradeCount > 0 && _ownerp == threadp);
    if (_readCount > 0) {
        /* this flag is set while the owner is waiting for a write
         * lock while holding an upgrade lock.
         */
        _upgradeToWrite = 1;

        /* to avoid fairness issues, make it look like we've been queued a while.  Also,
         * since we're still holding the upgrade lock while we're waiting to upgrade,
         * we need to be at the head of the writesWaiting queue, since we can't allow
         * any other writers in.
         */
        threadp->_lockClock = _lockClock - 100;
        _writesWaiting.prepend(threadp);

        threadp->sleep(&_lock);
        _lock.take();
    }
    else {
        /* no readers preventing our upgrade */
        _upgradeCount--;
        _writeCount++;
    }

    /* should clear by the time this call completes */
    _upgradeToWrite = 0;

    _lock.release();
}
