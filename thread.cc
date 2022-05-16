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
#include <iostream>

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "thread.h"
#include "Exception.h"


extern "C" {
extern int xgetcontext(ucontext_t *ctxp);
extern int xsetcontext(ucontext_t *ctxp);
};

pthread_key_t ThreadDispatcher::_dispatcherKey;
pthread_once_t ThreadDispatcher::_once = PTHREAD_ONCE_INIT;
ThreadDispatcher *ThreadDispatcher::_allDispatchers[ThreadDispatcher::_maxDispatchers];
uint16_t ThreadDispatcher::_dispatcherCount;

SpinLock Thread::_globalThreadLock;
dqueue<ThreadEntry> Thread::_allThreads;
dqueue<ThreadEntry> Thread::_joinThreads;
uint32_t Thread::_defaultStackSize = 128*1024;
int Thread::_trackStackUsage = 0;

ThreadMon *ThreadMon::_monp = 0;

Thread::TraceProc *Thread::_traceProcp;

/*****************Thread*****************/

/* internal function doing some of the initialization of a thread */
void
Thread::init(std::string name, uint32_t stackSize)
{
    if (stackSize == 0)
        _stackSize = _defaultStackSize;
    else
        _stackSize = stackSize;
    _goingToSleep = 0;
    _marked = 0;
    _globalThreadLock.take();
    _allEntry._threadp = this;
    _allThreads.append(&_allEntry);
    _globalThreadLock.release();
    _currentDispatcherp = NULL;
    _wiredDispatcherp = NULL;
    _blockingMutexp = NULL;
    _joinable = 0;
    _joiningThreadp = 0;
    _inJoinThreads = 0;
    _exitValuep = NULL;
    _exited = 0;
    _name = name;
    clock_gettime(CLOCK_REALTIME, &_createTs);
    _runTicks = 0;
    _stackp = (char *) malloc(_stackSize);
    if (_trackStackUsage)
        memset(_stackp, 0x7A, _stackSize);

    GETCONTEXT(&_ctx);
    _ctx.uc_link = NULL;
    _ctx.uc_stack.ss_sp = _stackp;
    _ctx.uc_stack.ss_size = _stackSize;
    _ctx.uc_stack.ss_flags = 0;
#if THREAD_PTR_FITS_IN_INT
    makecontext(&_ctx, (void (*)()) &ctxStart, 
                2,
                (int) ((long) this),
                0);
#else
    makecontext(&_ctx, (void (*)()) &ctxStart, 
                2,
                (int) (((long) this) & 0xFFFFFFFF),
                (int)(((long)this)>>32));
#endif
}

/* internal; called to start a light weight thread on a new stack */
/* static */ void
Thread::ctxStart(unsigned int p1, unsigned int p2)
{
    /* reconstruct thread pointer */
#if THREAD_PTR_FITS_IN_INT
    unsigned long threadInt = (unsigned long) p1;
#else
    unsigned long threadInt = (((unsigned long) p2)<<32) + (unsigned long)p1;
#endif
    Thread *threadp = (Thread *)threadInt;

    try {
        threadp->start();

        /* if the thread returns, just have it exit */
        threadp->exit(NULL);
    } catch (Exception &e) {
        GetExceptionDetails(std::cerr, e);
        assert("unhandled Exception"==nullptr);
    } catch (std::exception &e) {
        GetExceptionDetails(std::cerr, e);
        assert("unhandled std::exception"==nullptr);
    }
}

/* external: thread exits; if joinable, this doesn't delete the
 * thread; it is the responsibility of the joiner to delete the
 * thread.
 */
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
             * 
             * Note that we want the to ensure that the join doesn't
             * complete until we're off this stack, since the thread
             * that called join might delete the task (and thus our
             * stack) as soon as the join returns.  So, we use the
             * atomic sleep and release lock operation to make sure
             * we're back on the idle thread's stack, and note that
             * the join operation also obtains the globalThreadLock
             * before proceeding.
             */
            joinThreadp = _joiningThreadp;
            _joiningThreadp = NULL;
            joinThreadp->queue();
            sleep(&_globalThreadLock);
            printf("!back from sleep after thread=%p termination\n", this);
            assert(0);
        }
        else {
            /* joinable thread is waiting for the join call */
            assert(!_inJoinThreads);
            _joinThreads.append(&_joinEntry);
            _inJoinThreads = 1;
            sleep(&_globalThreadLock);
        }
    }
    else {
        /* non-joinable threads just self destruct */
        // printf("thread %p exiting\n", this);
        _currentDispatcherp->_helper.queueItem(/* queue this guy */ NULL, /* delete */ this);
        sleep(&_globalThreadLock);
    }
}

/* external: join with another thread, waiting for it to exit, if it
 * hasn't already exited.
 *
 * Note that this method is applied to the thread we want to join
 * with.  It blocks until the target thread exits.
 *
 * Applied to thread whose exit we're waiting for.
 */
int32_t
Thread::join(void **ptrpp)
{
    _globalThreadLock.take();
    assert(_joinable);
    if (!_exited) {
        _joiningThreadp = Thread::getCurrent();
        _joiningThreadp->sleep(&_globalThreadLock);

        /* note that reobtaining this lock here also ensures that we don't
         * return from join until the joined thread is executing in the idle
         * thread's context, so our caller deletes the thread, it won't interfere
         * with the joined thread's execution as it shuts down.
         */
        _globalThreadLock.take();
        assert(_exited);
        _globalThreadLock.release();
    }
    else {
        _globalThreadLock.release();
    }

    if (ptrpp)
        *ptrpp = _exitValuep;

    return 0;
}

void
Thread::displayStackUsage()
{
    ThreadEntry *entryp;
    Thread *threadp;
    uint32_t bytesUsed;
    uint32_t i;
    char *tp;

    if( _trackStackUsage) {
        for(entryp = _allThreads.head(); entryp; entryp=entryp->_dqNextp) {
            threadp = entryp->_threadp;
            for(i=0, tp=threadp->_stackp; i<threadp->_stackSize; i++, tp++)
                if (*tp != 0x7a)
                    break;
            bytesUsed = threadp->_stackSize - i;
            printf("Thread %s used %d bytes of its %d bytes\n",
                   threadp->_name.c_str(), bytesUsed, threadp->_stackSize);
        }
    }
    else {
        printf("Stack usage not being tracked.\n");
    }
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
    unsigned long ix;
    
    ix = (unsigned long) this;
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
    osp_assert(disp!=NULL);
    return disp->_currentThreadp;
}

Thread::~Thread()
{
    _globalThreadLock.take();
    _allThreads.remove(&_allEntry);
    if (_inJoinThreads) {
        _inJoinThreads = 0;
        _joinThreads.remove(&_joinEntry);
    }
    _globalThreadLock.release();

    if (_stackp) {
        free(_stackp);
    }
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

/* statics */
uint32_t ThreadDispatcher::_spinTicks = 2200000; /* default */

ThreadDispatcher::~ThreadDispatcher()
{
    delete _currentThreadp;
}

/* A hook for destructing pthread thread specific keys; this
 * is called whenever a thread exits and ensures that any
 * thread specific lwt state is cleaned up/deallocated
 */
void ThreadDispatcherCleanup(void *arg)
{
    ThreadDispatcher *p = (ThreadDispatcher*)(arg);
    delete p;
}

void ThreadDispatcher::globalInit()
{
    pthread_key_create(&_dispatcherKey, ThreadDispatcherCleanup);
}

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
    uint64_t currentTicks;

    while(1) {
        _runQueue._queueLock.take();
        newThreadp = _runQueue._queue.pop();
        currentTicks = threadCpuTicks();
        if (!newThreadp) {
            /* CPU runs at about 2000-3000 cpu ticks per usec.  If we want
             * to wait at least a millisecond, 3 million ticks is about right
             */
            if ((currentTicks - _lastDispatchTicks) < _spinTicks) {
                _runQueue._queueLock.release();
                continue;
            }

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
            _lastDispatchTicks = threadCpuTicks();
            _runQueue._queueLock.release();
            _currentThreadp = newThreadp;
            newThreadp->_currentDispatcherp = this;
            newThreadp->_lastStartTicks = threadCpuTicks();
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

    /* adjust run time */
    threadp->_runTicks += threadCpuTicks() - threadp->_lastStartTicks;

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
    return NULL;
}

bool
ThreadDispatcher::isLwt()
{
    if (pthread_getspecific(_dispatcherKey)) {
        return true;
    } else {
        return false;
    }
}
/* static */ void
ThreadDispatcher::pthreadTop(const char *namep)
{
    ThreadDispatcher *mainDisp;
    Thread *mainThreadp;
    std::string name;

    /* Ensure that this is only done once per thread */
    if (isLwt()) {
        return;
    }

    /* create a special dispatcher for a pthread, so we can do
     * threadmutex operations from this thread without having to queue
     * a special thread to do that work.
     */
    mainDisp = new ThreadDispatcher(1);
    pthread_setspecific(_dispatcherKey, mainDisp);
    if (namep)
        name = std::string(namep);
    else
        name = "Pthread top";
    mainThreadp = new ThreadMain(name);

    /* make it look like this dispatcher dispatched this thread; set
     * the wiredDispatcher field so that we always queue this to the
     * pthread's dispatcher, which is always running while our main
     * thread is asleep.
     */
    mainDisp->_currentThreadp = mainThreadp;
    mainThreadp->_currentDispatcherp = mainDisp;
    mainThreadp->_wiredDispatcherp = mainDisp;
}

/* External; utility function to create a number of dispatchers */
/* static */ void
ThreadDispatcher::setup(uint16_t ndispatchers, int32_t spinUsec)
{
    pthread_t junk;
    uint32_t i;
    uint32_t cpuCount;

    /* don't use more than ndispatchers, and always leave at least one CPU alone */
    cpuCount = getCpuCount();
    if (ndispatchers > cpuCount-1)
        ndispatchers = cpuCount - 1;

    if (spinUsec>=0) {
        _spinTicks = spinUsec * 2200; //TODO(thhicks):  need something better than this hack here
    }

    /* if we don't have many CPUs, don't risk slowing things down by having a dispatcher
     * spin before going idle.
     */
    if (cpuCount <= 2)
        _spinTicks = 0;

    /* setup monitoring system */
    new ThreadMon();

    for(i=0;i<ndispatchers;i++) {
        new ThreadDispatcher();
    }

    /* call each dispatcher's dispatch function on a separate pthread;
     * note that the ThreadDispatcher constructor filled in _allDispatchers
     * array.
     */
    for(i=0;i<ndispatchers;i++) {
        char thr_name[8];

        pthread_create(&junk, NULL, dispatcherTop, _allDispatchers[i]);
        snprintf(thr_name, sizeof(thr_name), "exec%d", i);
        pthread_setname_np(junk, thr_name);
    }

    pthreadTop("First thread");
}

/* Internal constructor to create a new dispatcher */
ThreadDispatcher::ThreadDispatcher(int special) {
    if (!special) {
        Thread::_globalThreadLock.take();
        _allDispatchers[_dispatcherCount++] = this;
        Thread::_globalThreadLock.release();
    }

    _sleeping = 0;
    _currentThreadp = NULL;
    _idle._disp = this;
    _pauseRequests = 0;
    _paused = 0;
    _lastDispatchTicks = 0;     /* last time a thread was dispatched */
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

/* static */ uint32_t
ThreadDispatcher::getCpuCount()
{

    char fname[] = "/proc/cpuinfo";
    char type[20];
    char value[1000];
    uint32_t cpuCount = 0;

    FILE *f = fopen(fname, "r");

    while (true) {
        int res = fscanf(f, "%20[^:]:%1000[^\n]\n", type, value);

        if (res < 0) {
            break;
        }
        /*
         * The assumption here is that cpuinfo will always have
         *     (^<type_str>\s?:( <value_str>)?$)|(^$)
         * If fscanf doesn't get <value_str> but has gotten <type_str>
         * it must be placed after the :, facing a \n
         * Thus in both cases, we eat the \n.
         */
        if (res < 2) {
            fscanf(f, "\n");
            continue;
        }

        /*
         * Our assumption is that processor comes first in the lineup.
         */
        if (strncmp(type, "processor", 9) == 0) {
            cpuCount++;
        }
    }

    fclose(f);

    /* keep things sane */
    if (cpuCount < 1)
        cpuCount = 1;

    return cpuCount;
}

/*****************Once*****************/
/* returns true if called the function, false if already called; note
 * that the spinlock in the Once structure is held over the
 * initialization call.  We do that so we can guarantee that no one
 * proceeds from the call until the first Once initialization function
 * has returned.
 */
int
Once::call(OnceProc *procp, void *contextp)
{
    int rval;

    _lock.take();
    if (!_called) {
        rval = 1;
        _called = 1;
        procp(contextp);
    }
    else
        rval = 0;
    _lock.release();
    
    return rval;
}

/*****************ThreadHelper****************/
void *
ThreadHelper::start()
{
    ThreadHelperItem *itemp;

    while(1) {
        _globalThreadLock.take();
        itemp = _items.pop();
        if (itemp == NULL) {
            _running = 0;
            sleep(&_globalThreadLock);
        }
        else {
            /* we have something to do */
            _globalThreadLock.release();
            if (itemp->_threadToFreep) {
                itemp->_threadToFreep->releaseThread();
            }
            if (itemp->_threadToQueuep) {
                itemp->_threadToQueuep->queue();
            }
            delete itemp;
        }
    }
}

/*****************ThreadMain*****************/
void
ThreadMain::queue()
{
    assert(_wiredDispatcherp != NULL);
    _wiredDispatcherp->queueThread(this);
}

/********************************Utilities********************************/
int
threadClockCmp( uint32_t a, uint32_t b)
{
    if (a == b)
        return 0;
    else if (((int32_t)(a - b)) < 0)
        return -1;
    else
        return 1;
}
