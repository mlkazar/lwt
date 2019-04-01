#ifndef __THREAD_H_ENV__
#define __THREAD_H_ENV__ 1

#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <atomic>

#include "dqueue.h"
#include "osp.h"

/* does a pointer fit in an integer? */
#if defined(__arm__)
#define THREAD_PTR_FITS_IN_INT    1
#define SETCONTEXT(x) xsetcontext(x)
#define GETCONTEXT(x) xgetcontext(x)

#elif defined(__x86_64__)

#define THREAD_PTR_FITS_IN_INT    0
#define SETCONTEXT(x) setcontext(x)
#define GETCONTEXT(x) getcontext(x)
#endif

class Thread;
class ThreadEntry;
class ThreadDispatcher;

/* a simple spin lock, available to external callers */
class SpinLock {
 public:
    std::atomic<int> _owningPid;

    SpinLock() {
        _owningPid = 0;
    }

    /* grab the lock */
    void take() {
        int exchangeValue;
        int newValue;

        while(1) {
            exchangeValue = 0;
            newValue = 1;
            if (_owningPid.compare_exchange_strong(exchangeValue, 
                                                   newValue,
                                                   std::memory_order_acquire)) {
                /* success */
                break;
            }
            else {
                continue;
            }
        }
    }

    /* return true if we get the lock, but never block */
    int tryLock() {
        int exchangeValue;
        int newValue;
        exchangeValue = 0;
        newValue = 1;
        if (_owningPid.compare_exchange_weak(exchangeValue, 1, std::memory_order_acquire)) {
            /* success */
            return 1;
        }
        else {
            return 0;
        }
    }

    /* release the lock */
    void release() {
        _owningPid.store(0, std::memory_order_release);
    }
};

/* use to allow construction of multiple lists of threads */
class ThreadEntry {
 public:
    ThreadEntry *_dqNextp;
    ThreadEntry *_dqPrevp;
    Thread *_threadp;       /* back ptr */
};

/* one of these per user thread.  A thread can only exist in one spot in any collection
 * of run queues, unlike Avere Tasks.
 */
class Thread {
    friend class ThreadDispatcher;

 public:
    typedef void (InitProc) (void *contextp, Thread *threadp);

    /* a list of all threads in existence, and a spin lock that
     * protects the _allThreads list, and that alone.
     */
    static dqueue<ThreadEntry> _allThreads;
    static SpinLock _globalThreadLock;

    /* for when thread is blocked, or when it is in a run queue, these pointers are
     * used.
     */
    Thread *_dqNextp;
    Thread *_dqPrevp;

    /* so we have a list of all threads that exist, so gdb can find them all */ 
    ThreadEntry _allEntry;

    /* the context used for stack switching; keep registers and PC when a user thread
     * isn't running.
     */
    ucontext_t _ctx;

    /* everything BEFORE this point is tracked in gdb, i.e. there's a structure in gdb
     * labeled kazar_thread that matches the earlier parts of this structure,
     * so that gdb can read the thread and setup a copy of the registers for 
     * debugging.
     */

    /* set to the current dispatcher when a thread is loaded onto a processor */
    ThreadDispatcher *_currentDispatcherp; /* current dispatcher for running thread */

 private:
    /* used by getcontext to differentiate between when the dispatcher calls it to
     * store the context, and when the thread is re-woken when the dispatcher reloads
     * the context.  
     *
     * Todo: we can get rid of this flag by having getcontext storing
     * a return value of 1 in the saved registers, so that a call to
     * getcontext that returns 0 means the old context was saved, and
     * we should switch the dispatcher to the idle thread to find more
     * work, and a return value of 1 would then mean simply return to
     * the caller, since the thread was being restarted by the
     * dispatcher.  In the ARM code at least, it is a one character
     * change, but we should wait until we can do the 64 bit x86
     * version at the same time.
     */
    int _goingToSleep;

    /* Internal C function called by the first activation of a thread
     * by makecontext.  Note that its signature is defined by the C
     * library, and we may have to split a context pointer across two
     * integers to get it to fit into ctxStart.  This function calls
     * the thread's virtual start method.
     */
    static void ctxStart(unsigned int p1, unsigned int p2);

 public:
    Thread() {
        _goingToSleep = 0;
        _globalThreadLock.take();
        _allThreads.append(&_allEntry);
        _globalThreadLock.release();
        _currentDispatcherp = NULL;

        init();
    }

    /* this is the main entry point to a thread.  The definer of a thread specifies this
     * when creating a thread, and it will start here the first time the thread
     * is queued.
     */
    virtual void start() = 0;

    /* this function is used by primitives to put a thread to sleep.
     * The idea is that if you have a task list protected by a spin
     * lock, you can hold that spin lock, remove the task from that
     * list, and call sleep.  Sleep will put your task to sleep and
     * then drop the lock.
     *
     * The goal of this interface is to prevent the race where a
     * thread releases a spinlock, making its thread ID visible, and
     * then goes to sleep. In the gap between the time the thread is
     * visible as sleeping, and the time it actually sleeps, it might
     * get queued again (even before its current state is saved),
     * causing all sorts of mayhem.
     */
    void sleep(SpinLock *lockp);

    /* queued to start a task that's been put to sleep, or freshly constructed */
    void queue();

    static Thread *getCurrent();

 private:
    /* internal function used in constructing a task */
    void init();

    void resume();
};

/* this thread provides a context for running the dispatcher, so that when a thread
 * blocks, we can run the dispatcher without staying on the same stack.
 */
class ThreadIdle : public Thread {
 public:
    SpinLock *_userLockToReleasep;
    ThreadDispatcher *_disp;

    void start();

    ThreadIdle() {
        _userLockToReleasep = NULL;
    }

    void setLock(SpinLock *lockp) {
        _userLockToReleasep = lockp;
    }

    SpinLock *getLockAndClear() {
        SpinLock *lockp = _userLockToReleasep;
        _userLockToReleasep = NULL;
        return lockp;
    }
};

class ThreadDispatcherQueue {
    friend class ThreadDispatcher;
    friend class Thread;

    dqueue<Thread> _queue;
    SpinLock _queueLock;
};

class ThreadDispatcher {
    friend class Thread;
    friend class ThreadDispatcherQueue;

 public:
    static const long _maxDispatchers=8;

 private:
    static pthread_once_t _once;
    static pthread_key_t _dispatcherKey;

    static ThreadDispatcher *_allDispatchers[_maxDispatchers];
    static uint16_t _dispatcherCount;

    /* queue of pending locks */
    ThreadDispatcherQueue _runQueue;

    Thread *_currentThreadp;
    int _sleeping;
    pthread_cond_t _runCV;
    pthread_mutex_t _runMutex;

    /* an idle thread that provides a thread with a stack on which we can run
     * the dispatcher.
     */
    ThreadIdle _idle;

    static void globalInit() {
        pthread_key_create(&_dispatcherKey, NULL);
    }

    static ThreadDispatcher *currentDispatcher();

    static void *dispatcherTop(void *ctx);

 public:
    /* called to put thread to sleep on current dispatcher, and then dispatch
     * more threads.
     */
    void sleep(Thread *threadp, SpinLock *lockp);

    /* queue this thread on this dispatcher */
    void queueThread(Thread *threadp);

    /* called to look for work in the run queue, or wait until some shows up */
    void dispatch();

    /* called to create a bunch of dispatchers and their pthreads */
    static void setup(uint16_t ndispatchers);

    ThreadDispatcher();
};

#endif /* __THREAD_H_ENV__ */ 
