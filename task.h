#ifndef __TASK_H_ENV__
#define __TASK_H_ENV__ 1

#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <pthread.h>
#include <atomic>

#include "dqueue.h"

/* does a pointer fit in an integer? */
#define TASK_PTR_FITS_IN_INT    1
//#define TASK_PTR_FITS_IN_INT    0

class Task;
class TaskEntry;
class TaskDispatcher;

class SpinLock {
 public:
    std::atomic<long> _owningPid;

    SpinLock() {
        _owningPid = 0;
    }

    void take() {
        long exchangeValue;
        long newValue;

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

    /* return true if we get the lock */
    int tryLock() {
        long exchangeValue;
        long newValue;
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

    void release() {
        _owningPid.store(0, std::memory_order_release);
    }
};

class TaskEntry {
 public:
    TaskEntry *_dqNextp;
    TaskEntry *_dqPrevp;
    Task *_taskp;       /* back ptr */
};

class Task {
 public:
    typedef void (InitProc) (void *contextp, Task *taskp);

    static dqueue<TaskEntry> _allTasks;
    static SpinLock _globalTaskLock;

    /* for when task is blocked */
    Task *_dqNextp;
    Task *_dqPrevp;

    TaskEntry _allEntry;
    ucontext_t _ctx;
    int _goingToSleep;

    static void ctxStart(int p1, int p2);

 public:
    Task() {
        _goingToSleep = 0;
        _globalTaskLock.take();
        _allTasks.append(&_allEntry);
        _globalTaskLock.release();

        init();
    }

    virtual void start() = 0;

    void init();

    void resume();

    void queue();
};

/* this task provides a context for running the dispatcher, so that when a task
 * blocks, we can run the dispatcher without staying on the same stack.
 */
class TaskIdle : public Task {
 public:
    SpinLock *_userLockToReleasep;
    TaskDispatcher *_disp;

    void start();

    TaskIdle() {
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

class TaskDispatcher
{
 public:
    static pthread_once_t _once;
    static pthread_key_t _dispatcherKey;
    static const long _maxDispatchers=8;

    static TaskDispatcher *_allDispatchers[_maxDispatchers];
    static uint16_t _dispatcherCount;

    dqueue<Task> _runQueue;
    SpinLock _runQueueLock;

    Task *_currentTaskp;
    int _sleeping;
    pthread_cond_t _runCV;
    pthread_mutex_t _runMutex;

    /* an idle task that provides a task with a stack on which we can run
     * the dispatcher.
     */
    TaskIdle _idle;

    static void globalInit() {
        pthread_key_create(&_dispatcherKey, NULL);
    }

    static TaskDispatcher *currentDispatcher();

    static void *dispatcherTop(void *ctx);

 public:
    /* called to put task to sleep on current dispatcher, and then dispatch
     * more tasks.
     */
    static void sleep(SpinLock *lockp);

    /* queue this task on this dispatcher */
    void queueTask(Task *taskp);

    /* called to look for work in the run queue, or wait until some shows up */
    void dispatch();

    /* called to create a bunch of dispatchers and their pthreads */
    static void setup(uint16_t ndispatchers);

    TaskDispatcher();
};

#endif /* __TASK_H_ENV__ */ 
