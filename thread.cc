#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <assert.h>

#include "thread.h"

extern "C" {
extern int xgetcontext(ucontext_t *ctxp);
extern int xsetcontext(ucontext_t *ctxp);
};

pthread_key_t ThreadDispatcher::_dispatcherKey;
pthread_once_t ThreadDispatcher::_once;
ThreadDispatcher *ThreadDispatcher::_allDispatchers[ThreadDispatcher::_maxDispatchers];
uint16_t ThreadDispatcher::_dispatcherCount;

SpinLock Thread::_globalThreadLock;
dqueue<ThreadEntry> Thread::_allThreads;
dqueue<ThreadEntry> Thread::_joinThreads;

/*****************Thread*****************/

/* internal function doing some of the initialization of a thread */
void
Thread::init()
{
    static const long memSize = 1024*1024;
    GETCONTEXT(&_ctx);
    _ctx.uc_link = NULL;
    _ctx.uc_stack.ss_sp = malloc(memSize);
    _ctx.uc_stack.ss_size = memSize;
    _ctx.uc_stack.ss_flags = 0;
#if THREAD_PTR_FITS_IN_INT
    makecontext(&_ctx, (void (*)()) &ctxStart, 
                2,
                (int) ((long) this),
                0);
#else
    makecontext(&_ctx, (void (*)()) &ctxStart, 
                2,
                (int) ((long) this) & 0xFFFFFFFF,
                (int)((long)this)>>32);
#endif
}

/* internal; called to start a light weight thread on a new stack */
/* static */ void
Thread::ctxStart(unsigned int p1, unsigned int p2)
{
    void *rcodep;
    /* reconstruct thread pointer */
#if THREAD_PTR_FITS_IN_INT
    unsigned long threadInt = (unsigned long) p1;
#else
    unsigned long threadInt = (((unsigned long) p2)<<32) + (unsigned long)p1;
#endif
    Thread *threadp = (Thread *)threadInt;

    rcodep = threadp->start();
    printf("Thread %p returned from start code=%p\n", threadp, rcodep);

    threadp->exit(rcodep);
}

/* external: thread exits */
void
Thread::exit(void *valuep)
{
    Thread *joinThreadp;
    _globalThreadLock.take();

    /* threads shouldn't exit multiple times */
    assert(!_exited);
    _exited = 1;
    _exitValuep = valuep;

    if (_joinable) {
        /* note that joinable threads are actually deleted by the join call, which must
         * eventuall get performed
         */
        if (_joiningThreadp) {
            /* Someone already did a join for us, and is waiting for our exit.
             * We're going to release the global lock and then queue the
             * thread that did the join.
             */
            joinThreadp = _joiningThreadp;
            _joiningThreadp = NULL;
            _globalThreadLock.release();
            joinThreadp->queue();
            sleep(NULL);
            printf("!back from sleep after thread=%p termination\n", this);
            assert(0);
        }
        else {
            /* joinable threads waiting for the join call */
            _joinThreads.append(&_joinEntry);
        }
    }
    else {
        /* non-joinable threads just self destruct */
        printf("thread exiting\n");
        delete this;
    }
    sleep(&_globalThreadLock);
}

/* external: join with another thread, waiting for it to exit, if it
 * hasn't already exited.
 *
 * Note that this method is applied to the thread we want to join
 * with.  It blocks until the target thread exits.
 */
int32_t
Thread::join(void **ptrpp)
{
    _globalThreadLock.take();
    assert(_joinable);
    if (!_exited) {
        _joiningThreadp = Thread::getCurrent();
        _joiningThreadp->sleep(&_globalThreadLock);
        assert(_exited);
    }
    else {
        _globalThreadLock.release();
    }

    *ptrpp = _exitValuep;
    delete this;
    return 0;
}

/* internal; called to resume a thread, or start it if it has never been run before */
void
Thread::resume()
{
    SETCONTEXT(&_ctx);
}

/* external, find a suitable dispatcher and queue the thread for it.  Has round-robin
 * policy built in for now.
 */
void
Thread::queue()
{
    unsigned int ix;
    
    ix = (unsigned int) this;
    ix = (ix % 127) % ThreadDispatcher::_dispatcherCount;
    ThreadDispatcher::_allDispatchers[ix]->queueThread(this);
}

/* external, put a thread to sleep and then release the spin lock */
void
Thread::sleep(SpinLock *lockp)
{
    _currentDispatcherp->sleep(this, lockp);
}

/* static */ Thread *
Thread::getCurrent() 
{
    ThreadDispatcher *disp = ((ThreadDispatcher *)
                              pthread_getspecific(ThreadDispatcher::_dispatcherKey));
    return disp->_currentThreadp;
}

/*****************ThreadIdle*****************/

/* internal idle thread whose context can be resumed; used to get off
 * of stack of thread going to sleep, so that if sleeping thread gets
 * woken immediately after the sleep lock is released, its use of its
 * own stack won't interfere with our continuing to run the
 * dispatcher.
 */
void *
ThreadIdle::start()
{
    SpinLock *lockp;
    
    while(1) {
        GETCONTEXT(&_ctx);
        lockp = getLockAndClear();
        if (lockp)
            lockp->release();
        _disp->dispatch();
    }
}

/*****************ThreadDispatcher*****************/

/* Internal; find a thread in our dispastcher's run queue, and resume
 * it.  Go to sleep if there are no runnable threads.
 * 
 * Should be called on idle thread, not a thread just about to sleep,
 * since otherwise we might be asleep when another CPU resumes the
 * thread above us on the stack.  Actually, this can happen even if we
 * don't go to sleep.
 */
void
ThreadDispatcher::dispatch()
{
    Thread *newThreadp;
    while(1) {
        _runQueue._queueLock.take();
        newThreadp = _runQueue._queue.pop();
        if (!newThreadp) {
            _sleeping = 1;
            _runQueue._queueLock.release();
            pthread_mutex_lock(&_runMutex);
            while(_sleeping || _pauseRequests) {
                if (_pauseRequests)
                    _paused = 1;
                pthread_cond_wait(&_runCV, &_runMutex);
            }
            pthread_mutex_unlock(&_runMutex);
        }
        else{
            _runQueue._queueLock.release();
            _currentThreadp = newThreadp;
            newThreadp->_currentDispatcherp = this;
            newThreadp->resume();   /* doesn't return */
        }
    }
}

/* External.  When a thread needs to block for some condition, the
 * paradigm is that it will have some SpinLock held holding invariant
 * some condition, such as the state of a mutex.  As soon as that spin
 * lock is released, another processor might see this thread in a lock
 * queue, and queue it to a dispatcher.  We need to ensure that the
 * thread's state is properly stored before allowing a wakeup
 * operation (queueThread) to begin.
 *
 * In the context of this function, this means we must finish the
 * getcontext call and the clearing of _goingToSleep before allowing
 * the thread to get queued again, so that when the thread is resumed
 * after the getcontext call, it will simply return to the caller.
 *
 * Note that this means that the unlock will get performed by the
 * same dispatcher as obtained the spin lock, but when sleep returns,
 * it may be running on a different dispatcher.
 */
void
ThreadDispatcher::sleep(Thread *threadp, SpinLock *lockp)
{
    assert(threadp == _currentThreadp);
    _currentThreadp = NULL;
    threadp->_goingToSleep = 1;
    GETCONTEXT(&threadp->_ctx);
    if (threadp->_goingToSleep) {
        threadp->_goingToSleep = 0;

        /* prepare to get off this stack, so if this thread gets resumed
         * after we drop the user's spinlock, we're not using this
         * stack any longer.
         *
         * The idle context will resume at ThreadIdle::start, either at
         * the start or in the while loop, and will then dispatch the
         * next thread from the run queue.
         */
        _idle._userLockToReleasep = lockp;
        SETCONTEXT(&_idle._ctx);

        printf("!Error: somehow back from sleep's setcontext disp=%p\n", this);
    }
    else {
        /* this thread is being woken up */
        return;
    }
}

/* Internal; call to the dispatcher to queue a task to this dispatcher */
void
ThreadDispatcher::queueThread(Thread *threadp)
{
    _runQueue._queueLock.take();
    _runQueue._queue.append(threadp);
    if (_sleeping) {
        _runQueue._queueLock.release();
        pthread_mutex_lock(&_runMutex);
        _sleeping = 0;
        pthread_mutex_unlock(&_runMutex);
        pthread_cond_broadcast(&_runCV);
    }
    else {
        _runQueue._queueLock.release();
    }
}

/* Internal -- first function called in a dispatcher's creation */
/* static */ void *
ThreadDispatcher::dispatcherTop(void *ctx)
{
    ThreadDispatcher *disp = (ThreadDispatcher *)ctx;
    pthread_setspecific(_dispatcherKey, disp);
    disp->_idle.resume(); /* idle thread switches to new stack and then calls the dispatcher */
    printf("Error: dispatcher %p top level return!!\n", disp);
}

/* External; utility function to create a number of dispatchers */
/* static */ void
ThreadDispatcher::setup(uint16_t ndispatchers)
{
    pthread_t junk;
    uint32_t i;

    for(i=0;i<ndispatchers;i++) {
        new ThreadDispatcher();
    }

    /* call each dispatcher's dispatch function on a separate pthread;
     * note that the ThreadDispatcher constructor filled in _allDispatchers
     * array.
     */
    for(i=0;i<ndispatchers;i++) {
        pthread_create(&junk, NULL, dispatcherTop, _allDispatchers[i]);
    }
}

/* Internal constructor to create a new dispatcher */
ThreadDispatcher::ThreadDispatcher() {
    Thread::_globalThreadLock.take();
    _allDispatchers[_dispatcherCount++] = this;
    Thread::_globalThreadLock.release();

    _sleeping = 0;
    _currentThreadp = NULL;
    _idle._disp = this;
    _pauseRequests = 0;
    _paused = 0;
    pthread_mutex_init(&_runMutex, NULL);
    pthread_cond_init(&_runCV, NULL);
    pthread_cond_init(&_pauseCV, NULL);
    pthread_once(&_once, &ThreadDispatcher::globalInit);
}

/* pause dispatching for a dispatcher; when the dispatcher is about to
 * go idle, the dispatcher checks for _pauseRequests, and waits for
 * the count to go to zero.  It also wakes up the pauseCV after
 * setting _paused.
 */
void
ThreadDispatcher::pauseDispatching()
{
    pthread_mutex_lock(&_runMutex);
    _pauseRequests++;
    pthread_mutex_unlock(&_runMutex);
}

void
ThreadDispatcher::resumeDispatching()
{
    int doWakeup = 0;
    pthread_mutex_lock(&_runMutex);
    assert(_pauseRequests > 0);
    _pauseRequests--;
    if (_pauseRequests == 0) {
        doWakeup = 1;
        _paused = 0;
    }
    pthread_mutex_unlock(&_runMutex);

    /* and make sure that the dispatcher knows that it is time to run again */
    if (doWakeup)
        pthread_cond_broadcast(&_runCV);
}

/* static */ void
ThreadDispatcher::pauseAllDispatching()
{
    uint32_t i;
    ThreadDispatcher *disp;

    for(i=0; i<_maxDispatchers; i++) {
        disp = _allDispatchers[i];
        if (!disp)
            break;
        disp->pauseDispatching();
    }
}

/* return true if all dispatchers have stopped; if they're stopped and we've already
 * called pauseAllDispatching, then they'll stay paused.
 */
/* static */ int
ThreadDispatcher::pausedAllDispatching()
{
    uint32_t i;
    ThreadDispatcher *disp;
    int rcode;
    int isSleeping;

    rcode = 1;

    for(i=0; i<_maxDispatchers; i++) {
        disp = _allDispatchers[i];
        if (!disp)
            break;
        isSleeping = disp->isSleeping();
        if (!isSleeping) {
            rcode = 0;
            break;
        }
    }

    return rcode;
}

/* static */ void
ThreadDispatcher::resumeAllDispatching()
{
    uint32_t i;
    ThreadDispatcher *disp;

    for(i=0; i<_maxDispatchers; i++) {
        disp = _allDispatchers[i];
        if (!disp)
            break;
        disp->resumeDispatching();
    }
}
