#ifndef _THREADPOOL_H_ENV__
#define _THREADPOOL_H_ENV__ 1

#include "thread.h"
#include "threadmutex.h"

/* usage: Create a ThreadPool, providing a factory class that just
 * creates your worker threads in its newWorker method, and providing
 * a maximum number of threads that csn exist at a given time.
 *
 * Workers start in idle state, meaning they're ready to be allocated.
 * They move into the allocated state when they've been allocated but
 * not yet started.  Once started, they move into running state.  Once
 * they complete execution, they move into the join state, and once
 * joined, they move back into idle state.  A worker (or its owner)
 * can specify that it go directly into idle state instead of waiting
 * for a tpJoin call by calling tpIdleOnExit on itself.
 * 
 * To allocate a worker, you call tpGet, which returns an idle worker
 * or TP_ERR_ALL_RUNNING.  The caller can then call any methods they
 * want in the worker's subclass.  When they're done, the caller calls
 * tpResume on the worker, which starts the thread executing again at
 * its tpStart method.  When the tpStart method returns, the worker
 * goes into a completed queue, where someone can join with the
 * thread.
 *
 * The caller typically calls tpJoin on the worker to wait for the
 * specific worker to return from its tpStart function. Alternatively,
 * the caller can call tpJoinAny, which will return any completed
 * ThreadPool worker, and its return value.  Asynchronous calls can be
 * made where TP_ERR_ALL_DONE or TP_ERR_ALL_RUNNING may be returned,
 * the former indicating that all the workers are done, and the latter
 * indicating all workers are either idle or still running.
 *
 * As mentioned above, A worker can also can call tpIdleOnExit to
 * indicate that the worker thread should go directly into the idle
 * queue again when tpStart returns, rather than waiting for someone
 * to call ThreadPool::Worker::join.
 */

class ThreadPool {
 public:
    enum Error {
        TP_OK = 0,
        TP_ERR_ALL_RUNNING = 1,
        TP_ERR_ALL_DONE = 2,
        TP_ERR_SHUTDOWN = 3
    };

    enum State {
        TP_NONE = 0,    /* not in any queue */
        TP_IDLE = 1,
        TP_ACTIVE = 2,
        TP_JOIN = 3,
        TP_JOINED = 4
    };

    /* a worker goes through a set of states:
     * idle -- waiting for a get operation to return this worker
     * active -- get done, executing tpStart method
     * join -- waiting for a join operation to return this (optional)
     * joined -- join done, waiting for tpFinished (optional)
     *
     * after tpFinished, we're back at idle.  If idleOnExit is set,
     * we skip join and joined and go directly back to idle
     */
    class Worker : public Thread {
    public:
        Worker *_dqNextp;
        Worker *_dqPrevp;
        ThreadPool *_poolp;
        State _state;
        uint8_t _didInit;
        uint8_t _waitForJoin;
        void *_joinReturnValuep;

        uint8_t _resumeDone;
        uint8_t _finishedDone;

        /* user sleeps here waiting for this thread to be joinable */
        ThreadCond _joinOneReadyCv;

        /* worker sleeps here waiting for work to arrive */
        ThreadCond _resumeCv;

        /* worker sleeps here waiting for tpFinished */
        ThreadCond _finishedCv;

        virtual void *tpStart() = 0;

        void *start();

        void *tpJoin();

        void tpIdleOnExit() {
            _waitForJoin = 0;
        }

        int getIdleOnExit() {
            return _waitForJoin;
        }

        void init(ThreadPool *poolp);

        void tpResume();

        void tpFinished();

        Worker() {
            _state = TP_NONE;
            _didInit = 0;
        }
    };

    class WorkerFactory {
    public:
        virtual Worker *newWorker() = 0;
    };

    ThreadMutex _lock;
    WorkerFactory *_factoryp;
    uint32_t _nthreads;
    uint32_t _createdThreads;
    dqueue<Worker> _idleThreads;
    dqueue<Worker> _activeThreads;
    dqueue<Worker> _joinThreads;
    uint8_t _shutdown;

    /* user sleeps here waiting for a thread to add itself to the idle queue */
    ThreadCond _idleCv;
    
    /* user sleeps here waiting for a thread to be joinable */
    ThreadCond _joinReadyCv;

    void init(uint32_t nthreads, WorkerFactory *factoryp) {
        _factoryp = factoryp;
        _nthreads = nthreads;
        _idleCv.setMutex(&_lock);
        _joinReadyCv.setMutex(&_lock);
        _shutdown = 0;
        _createdThreads = 0;
    };

    /* can return TP_ERR_ALL_RUNNING if can't allocating and wait==0 */
    int32_t get(Worker **workerpp, int wait=1);

    int32_t joinAny(Worker **workerpp, void **returnValuep, int wait = 1);

    /* once shutdown, all pending get functions terminate, even if wait was set */
    void shutdown();
};

#endif /* _THREADPOOL_H_ENV__ */
