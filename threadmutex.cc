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
ThreadCond::wait(ThreadMutex *mutexp)
{
    Thread *mep = Thread::getCurrent();

    /* keep track which mutex is associated with this condition variable */
    if (_mutexp == NULL)
        _mutexp = mutexp;
    else if (mutexp == NULL) {
        mutexp = _mutexp;
    }
    else {
        assert(_mutexp == mutexp);
    }

    /* grab the spinlock protecting te mutex, and put ourselves to sleep
     * on the CV.  Finally, drop the mutex.
     */
    mutexp->_lock.take();
    assert(mep == mutexp->_ownerp);
    
    /* queue our task for the CV */
    _waiting.append(mep);

    /* and block, releasing the associated mutex */
    mutexp->releaseAndSleep(mep);

    /* and reobtain it on the way back out */
    mutexp->take();
}

/* wakeup a single waiting thread */
void
ThreadCond::signal()
{
    Thread *headp;
    _mutexp->_lock.take();
    headp = _waiting.pop();
    _mutexp->_lock.release();

    headp->queue();
}

/* wakeup all waiting threads */
void
ThreadCond::broadcast()
{
    Thread *headp;
    Thread *nextp;

    _mutexp->_lock.take();
    headp = _waiting.head();
    _waiting.init();
    _mutexp->_lock.release();

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
        threadp->sleep(&_lock);
        _lock.take();
    }
    else {
        /* we can get the lock immediately, and w/o any fairness issues; we know
         * there are no fairness issues because if anyone was waiting for a lock,
         * someone would have to actually be holding the lock now, and no one is.
         */
        _fairness = 1; /* last granted lock was a write lock */
        _ownerp = threadp;
        _writeCount++;
    }

    /* record our ownership information, if any */
    if (trackerp) {
        trackerp->_lockMode = ThreadLockTracker::_lockWrite;
        trackerp->_threadp = threadp;
        _trackerQueue.append(trackerp);
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
         _fairness = 1;
         _ownerp = threadp;
         _writeCount++;
    }

    /* record our ownership information, if any */
    if (trackerp) {
        trackerp->_lockMode = ThreadLockTracker::_lockWrite;
        trackerp->_threadp = threadp;
        _trackerQueue.append(trackerp);
    }

    _lock.release();
    return 1;
}

/* must be called with the internal _lock held for the read/write lock */
void
ThreadLockRw::wakeNext()
{
    Thread *threadp;

    /* check the upgrade request first */
    if ( _upgradeCount > 0 && _readCount == 0 && _upgradeToWrite) {
        assert(_ownerp);
        _upgradeCount = 0;
        _upgradeToWrite = 0;
        _writeCount++;
        _ownerp->queue();
        /* the owner now has a write lock */
        return;
    }

    if (_fairness == 1) {
        /* last lock owner was a writer, so wakeup waiting readers and first
         * upgrade lock, if any.  Start with the readers.
         */
        if (_writeCount == 0 && !_upgradeToWrite) {
            while((threadp = _readsWaiting.pop()) != NULL) {
                _readCount++;
                _fairness = 0;
                threadp->queue();
            }
        }

        if (_ownerp == NULL) {
            /* neither write or upgrade locked, see if we can grant an upgrade lock */
            if ((threadp = _upgradesWaiting.pop()) != NULL) {
                _ownerp = threadp;
                _upgradeCount++;
                _upgradeToWrite = 0;
                _fairness = 0;
                threadp->queue();
                return;
            }
        }

        threadp = _writesWaiting.head();
        if ( threadp &&
             (_readCount == 0 && _ownerp == NULL)) {
            /* we have a writer waiting and no conflicts */
            _ownerp = threadp;
            _writesWaiting.remove(threadp);
            _writeCount++;
            threadp->queue();
        } /* no readers */
        return;
    }
    else {
        /* last lock owner was some number of readers, and maybe an
         * upgrader; must check writers first.  Note that the
         * writesWaiting queue includes upgraders waiting to get a
         * write lock.  If there are readers or upgraders waiting,
         * don't run them unless a writer gets a shot first.
         */
        threadp = _writesWaiting.head();
        if ( threadp &&
             (_readCount == 0 && _ownerp == NULL)) {
            /* we have a writer waiting and no conflicts */
            _ownerp = threadp;
            _writesWaiting.remove(threadp);
            _writeCount++;
            threadp->queue();
            return;
        } /* no readers */

        /* if we get here, we have no waiting writers, so we can let
         * readers / upgrades in, and those are all we actually have
         * waiting.  Note that if we have a pending upgradeToWrite,
         * we don't want to let any more readers in, to avoid starving
         * the upgrade.
         */
        if (_writeCount > 0 || _upgradeToWrite)
            return;
        
        while((threadp = _readsWaiting.pop()) != NULL) {
            _readCount++;
            _fairness = 0;
            threadp->queue();
        }

        if (_ownerp == NULL &&
            ((threadp = _upgradesWaiting.pop()) != NULL)) {
            /* grant an upgrade lock */
            _ownerp = threadp;
            _upgradeCount++;
            _upgradeToWrite = 0;
            _fairness = 0;
            threadp->queue();
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

    /* cleanup tracker tracking state, if any */
    if (trackerp) {
        _trackerQueue.remove(trackerp);
    }

    wakeNext();
    _lock.release();
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

    /* cleanup tracker tracking state, if any */
    if (trackerp) {
        _trackerQueue.remove(trackerp);
    }

    wakeNext();
    _lock.release();
}

void
ThreadLockRw::lockRead(ThreadLockTracker *trackerp)
{
    Thread *threadp;

    threadp = Thread::getCurrent();

    _lock.take();
    
    if (_fairness == 0) {
        /* if we have a writer waiting, and we last granted readers,
         * so we can't grant any more readers now.
         */
        if (_writesWaiting.head()) {
            _readsWaiting.append(threadp);
            threadp->sleep(&_lock);

            if (trackerp) {
                _lock.take();
                _trackerQueue.append( trackerp);
                _lock.release();
            }
            return;
        }
    }

    /* no writers waiting, we can add a read lock */
    if (_writeCount == 0) {
        if (trackerp) {
            _trackerQueue.append(trackerp);
        }
        _readCount++;
        _lock.release();
        return;
    }

    _readsWaiting.append(threadp);
    threadp->sleep(&_lock);

    if (trackerp) {
        _lock.take();
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
        if (trackerp)
            _trackerQueue.append(trackerp);
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
        _trackerQueue.remove( trackerp);
    }

    wakeNext();

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

    if (trackerp)
        _trackerQueue.append(trackerp);
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
