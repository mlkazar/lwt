#include "thread.h"
#include "threadmutex.h"
#include <assert.h>

/*****************ThreadMutex*****************/
void
ThreadMutex::take() {
    Thread *mep;

    mep = Thread::getCurrent();
    _lock.take();

    /* go in a loop waiting for a null owner, and then claim the lock;
     * note that when we're woken from the waiting queue, we must grab
     * the lock if it is available, since the release code will only wakeup
     * one sleeper at a time.
     */
    assert(_ownerp != mep);
    while(_ownerp != NULL) {
        _waiting.append(mep);
        mep->sleep(&_lock);
        _lock.take();
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
