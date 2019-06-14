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
    int sweepIx = 0;
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
ThreadMutexDetect::sweepFrom(Thread *threadp, int sweepIx)
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
    else {
        assert(_mutexp == mutexp);
    }

    /* grab the spinlock protecting te mutex, and put ourselves to sleep
     * on the CV.  Finally, drop the mutex.
     */
    _mutexp->_lock.take();
    assert(mep == mutexp->_ownerp);
    
    /* queue our task for the CV */
    _waiting.append(mep);

    /* and block, releasing the associated mutex */
    _mutexp->releaseAndSleep(mep);

    /* and reobtain it on the way back out */
    _mutexp->take();
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
