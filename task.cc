#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

#include "task.h"

pthread_key_t TaskDispatcher::_dispatcherKey;
pthread_once_t TaskDispatcher::_once;
TaskDispatcher *TaskDispatcher::_allDispatchers[TaskDispatcher::_maxDispatchers];
uint16_t TaskDispatcher::_dispatcherCount;

SpinLock Task::_globalTaskLock;
dqueue<TaskEntry> Task::_allTasks;

/* internal function doing some of the initialization of a task */
void
Task::init()
{
    static const long memSize = 1024*1024;
    getcontext(&_ctx);
    _ctx.uc_link = NULL;
    _ctx.uc_stack.ss_sp = malloc(memSize);
    _ctx.uc_stack.ss_size = memSize;
    _ctx.uc_stack.ss_flags = 0;
#if TASK_PTR_FITS_IN_INT
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

/* called to start a light weight task */
/* static */ void
Task::ctxStart(int p1, int p2)
{
    /* reconstruct task pointer */
#if TASK_PTR_FITS_IN_INT
    long taskInt = (long) p1;
#else
    long taskInt = (((long) p2)<<32) + (long)p1;
#endif
    Task *taskp = (Task *)taskInt;

    taskp->start();
    printf("Task %p returned from start\n", taskp);
}

/* called to resume a task, or start it if it has never been run before */
void
Task::resume()
{
    setcontext(&_ctx);
}

/* find a suitable dispatcher and queue the task for it */
void
Task::queue()
{
    int ix;
    
    ix = (int) this;
    ix = (ix % 127) % TaskDispatcher::_dispatcherCount;
    TaskDispatcher::_allDispatchers[ix]->queueTask(this);
}

/* idle task whose context can be resumed */
void
TaskIdle::start()
{
    SpinLock *lockp;
    
    while(1) {
        getcontext(&_ctx);
        lockp = getLockAndClear();
        if (lockp)
            lockp->release();
        _disp->dispatch();
    }
}

/* should we switch to an idle task before sleeping?  Probably, since
 * otherwise we might be asleep when another CPU resumes the task
 * above us on the stack.  Actually, this can happen even if we don't
 * go to sleep.  We really have to be off this stack before we call
 * drop the 
 */
void
TaskDispatcher::dispatch()
{
    Task *newTaskp;
    while(1) {
        _runQueueLock.take();
        newTaskp = _runQueue.pop();
        if (!newTaskp) {
            _sleeping = 1;
            _runQueueLock.release();
            pthread_mutex_lock(&_runMutex);
            while(_sleeping) {
                pthread_cond_wait(&_runCV, &_runMutex);
            }
            pthread_mutex_unlock(&_runMutex);
        }
        else{
            _runQueueLock.release();
            _currentTaskp = newTaskp;
            newTaskp->resume();   /* doesn't return */
        }
    }
}

/* When a task needs to block for some condition, the paradigm is that
 * it will have some SpinLock held holding invariant some condition,
 * such as the state of a mutex.  As soon as that spin lock is
 * released, another processor might see this task in a lock queue,
 * and queue it to a dispatcher.  We need to ensure that the task's
 * state is properly stored before allowing a wakeup operation
 * (queueTask) to begin.
 *
 * In the context of this function, this means we must finish the
 * getcontext call and the clearing of _goingToSleep before allowing
 * the task to get queued again, so that when the task is resumed
 * after the getcontext call, it will simply return to the caller.
 *
 * Note that this means that the unlock will get performed by the
 * same dispatcher as obtained the spin lock, but when sleep returns,
 * it may be running on a different dispatcher.
 */
/* static */ void
TaskDispatcher::sleep(SpinLock *lockp)
{
    TaskDispatcher *disp = (TaskDispatcher *) pthread_getspecific(TaskDispatcher::_dispatcherKey);
    Task *taskp;
    
    taskp = disp->_currentTaskp;
    disp->_currentTaskp = NULL;

    taskp->_goingToSleep = 1;
    getcontext(&taskp->_ctx);
    if (taskp->_goingToSleep) {
        taskp->_goingToSleep = 0;

        /* prepare to get off this stack, so if this task gets resumed
         * after we drop the user's spinlock, we're not using this
         * stack any longer.
         *
         * The idle context will resume at TaskIdle::start, either at
         * the start or in the while loop, and will then dispatch the
         * next task from the run queue.
         */
        disp->_idle._userLockToReleasep = lockp;
        setcontext(&disp->_idle._ctx);

        printf("!Error: somehow back from sleep's setcontext disp=%p\n", disp);
    }
    else {
        /* this task is being woken up */
        return;
    }
}

/* static */ TaskDispatcher *
TaskDispatcher::currentDispatcher()
{
    TaskDispatcher *disp = (TaskDispatcher *) pthread_getspecific(TaskDispatcher::_dispatcherKey);
    return disp;
}

void
TaskDispatcher::queueTask(Task *taskp)
{
    _runQueueLock.take();
    _runQueue.append(taskp);
    if (_sleeping) {
        _runQueueLock.release();
        pthread_mutex_lock(&_runMutex);
        _sleeping = 0;
        pthread_mutex_unlock(&_runMutex);
        pthread_cond_broadcast(&_runCV);
    }
    else {
        _runQueueLock.release();
    }
}

/* static */ void *
TaskDispatcher::dispatcherTop(void *ctx)
{
    TaskDispatcher *disp = (TaskDispatcher *)ctx;
    pthread_setspecific(_dispatcherKey, disp);
    disp->_idle.resume(); /* idle task switches to new stack and then calls the dispatcher */
    printf("Error: dispatcher %p top level return!!\n", disp);
}

/* the setup function creates a set of dispatchers */
/* static */ void
TaskDispatcher::setup(uint16_t ndispatchers)
{
    pthread_t junk;
    uint32_t i;

    for(i=0;i<ndispatchers;i++) {
        new TaskDispatcher();
    }

    /* call each dispatcher's dispatch function on a separate pthread;
     * note that the TaskDispatcher constructor filled in _allDispatchers
     * array.
     */
    for(i=0;i<ndispatchers;i++) {
        pthread_create(&junk, NULL, dispatcherTop, _allDispatchers[i]);
    }
}

TaskDispatcher::TaskDispatcher() {
    Task::_globalTaskLock.take();
    _allDispatchers[_dispatcherCount++] = this;
    Task::_globalTaskLock.release();

    _sleeping = 0;
    _currentTaskp = NULL;
    _idle._disp = this;
    pthread_mutex_init(&_runMutex, NULL);
    pthread_cond_init(&_runCV, NULL);
    pthread_once(&_once, &TaskDispatcher::globalInit);
}
