#include "threadpool.h"

int32_t
ThreadPool::get(Worker **workerpp, int wait)
{
    int32_t code = 0;
    Worker *workerp;

    _lock.take();
    while(1) {
        workerp = _idleThreads.pop();
        if (!workerp) {
            if (_createdThreads < _nthreads) {
                /* create a worker if we're not at our limit */
                _createdThreads++;
                _lock.release();
                workerp = _factoryp->newWorker();
                workerp->init(this);
                *workerpp = workerp;
                _lock.take();

                /* we want to wait until workerp gets to its idle
                 * state; we can't use workerp immediately, since
                 * workerp is still initializing itself.
                 */
                if (_idleThreads.head() == NULL && wait) {
                    _idleCv.wait();
                }
            }
            else if (wait) {
                /* at limit, wait until someone becomes idle */
                _idleCv.wait();
            }
            else {
                /* can't wait and no workers are ready */
                code = TP_ERR_ALL_RUNNING;
                workerp = NULL;
                break;
            }
        }
        else {
            /* we popped off an idle thread */
            workerp->_state = TP_NONE;
            *workerpp = workerp;
            break;
        }
    }

    if (code == 0) {
        osp_assert(workerp->_state == TP_NONE);
        workerp->_state = TP_ACTIVE;
        _activeThreads.append(workerp);
    }
    _lock.release();

    return code;
}

int32_t
ThreadPool::joinAny(Worker **workerpp, void **returnValuepp, int wait)
{
    Worker *workerp = NULL;
    int32_t code = 0;

    _lock.take();
    while(1) {
        workerp = _joinThreads.pop();
        if (!workerp) {
            if (!wait) {
                code = TP_ERR_ALL_DONE;
                break;
            }
            _joinReadyCv.wait();
            if (_shutdown) {
                _lock.release();
                return TP_ERR_SHUTDOWN;
            }
        }
        else {
            break;
        }
    }
    _lock.release();

    *workerpp = workerp;
    if (workerp) {
        *returnValuepp = workerp->_joinReturnValuep;
        workerp->_state = TP_JOINED;
    }
    else {
        *returnValuepp = NULL;
    }
    return code;
}

/* once you call start, the thread is available */
void
ThreadPool::Worker::init(ThreadPool *poolp)
{
    _poolp = poolp;
    _state = TP_NONE;
    _didInit = 1;
    _joinOneReadyCv.setMutex( &poolp->_lock);
    _resumeCv.setMutex( &poolp->_lock);
    _finishedCv.setMutex(&poolp->_lock);
    queue();    /* resume at our start function */
}

/* worker starts here after init is called */
void *
ThreadPool::Worker::start()
{
    /* init function sets first values of things that stay the same between activations,
     * such as associated mutex for cond variables.
     */
    printf("Worker thread=%p created\n", this);
    _poolp->_lock.take();
    while(1) {
        /* at top, worker is not in any queue, and has no more work to do */
        if (_state == TP_NONE) {
            _state = TP_IDLE;
            _poolp->_idleThreads.append(this);
            _poolp->_idleCv.broadcast();
        }

        /* set initial state up for this activation; note that all this code will run
         * before any other thread can allocate this thread from the idleThreads queue,
         * since we don't drop the lock until we wait for resumed below.
         */
        _waitForJoin = 1;
        _resumeDone = 0;
        _finishedDone = 0;
        _joinReturnValuep = NULL;

        /* wait for a new request */
        while(!_resumeDone) {
            _resumeCv.wait();
            if (_poolp->_shutdown){
                _poolp->_lock.release();
                return NULL;
            }
        }

        assert(_state == TP_ACTIVE);

        /* make the callout, and save the response; call without holding
         * any locks.
         */
        _poolp->_lock.release();
        _joinReturnValuep = tpStart();
        _poolp->_lock.take();

        /* move to inval state */
        _state = TP_NONE;
        _poolp->_activeThreads.remove(this);

        if (_waitForJoin) {
            /* go into the join queue */
            _state = TP_JOIN;
            _poolp->_joinThreads.append(this);

            /* don't know if someone's going to wait for any thread,
             * or this one, so signal both.  Signalling a condition
             * variable with no waiters is pretty inexpensive.
             */
            _poolp->_joinReadyCv.broadcast();
            _joinOneReadyCv.broadcast();

            /* wait for the guy who joined with us to release us */
            while(!_finishedDone) {
                _finishedCv.wait();
                if (_poolp->_shutdown) {
                    _poolp->_lock.release();
                    return NULL;
                }
            }

            /* join and finished done, and we're not in any queue now.
             * Go back to TP_NONE state and look for more work.
             */
            _state = TP_NONE;
        }
    }   /* loop forever waiting for reuse */
}

void *
ThreadPool::Worker::tpJoin()
{
    osp_assert(_waitForJoin);

    _poolp->_lock.take();
    while(_state != TP_JOIN) {
        /* sleep until the right state */
        _joinOneReadyCv.wait();
        if (_poolp->_shutdown) {
            break;
        }
    }

    _poolp->_joinThreads.remove(this);
    _state = TP_JOINED;

    _poolp->_lock.release();

    return (_poolp->_shutdown? NULL : _joinReturnValuep);
}

void
ThreadPool::Worker::tpResume()
{
    _poolp->_lock.take();
    _resumeDone = 1;
    _poolp->_lock.release();

    _resumeCv.broadcast();
}

void
ThreadPool::Worker::tpFinished()
{
    _poolp->_lock.take();
    _finishedDone = 1;
    _poolp->_lock.release();
    _finishedCv.broadcast();
}

void
ThreadPool::shutdown()
{
    Worker *workerp;

    _lock.take();
    _shutdown = 1;
    
    _idleCv.broadcast();
    _joinReadyCv.broadcast();

    for(workerp = _activeThreads.head(); workerp; workerp=workerp->_dqNextp) {
        workerp->_joinOneReadyCv.broadcast();
        workerp->_resumeCv.broadcast();
    }
    for(workerp = _joinThreads.head(); workerp; workerp=workerp->_dqNextp) {
        workerp->_joinOneReadyCv.broadcast();
        workerp->_resumeCv.broadcast();
    }
    _lock.release();
}
