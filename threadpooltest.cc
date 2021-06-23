#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "threadtimer.h"
#include "threadpool.h"

class TestWorker : public ThreadPool::Worker {
public:
    const char *_msgp;
    uint32_t _inValue;
    uint32_t _outValue;

    void setParms(const char *msgp, uint32_t inValue) {
        _msgp = msgp;
        _inValue = inValue;
    }

    void *tpStart() {
        _outValue = _inValue * _inValue;
        printf("Base setup %d %d joinExpected=%d\n", _inValue, _outValue, getIdleOnExit());
        ThreadTimer::sleep(400 + (random() % 11));
        return NULL;
    }
};

class TestWorkerFactory : public ThreadPool::WorkerFactory {
public:
    ThreadPool::Worker *newWorker() {
        return new TestWorker();
    }
};

class TestReceiver : public Thread {
public:
    ThreadPool *_testPoolp;
    uint32_t _count;
    ThreadMutex _lock;
    uint8_t _allStarted;
    ThreadCond _countCv;

    void init(ThreadPool *testPoolp) {
        _testPoolp = testPoolp;
        _count = 0;
        _allStarted = 0;
        _countCv.setMutex(&_lock);
        queue();
    }

    void allStarted() {
        _lock.take();
        _allStarted = 1;
        _lock.release();

        _countCv.broadcast();
    }

    void oneMoreStarted() {
        _lock.take();
        _count++;
        _lock.release();

        _countCv.broadcast();
    }

    void *start() {
        TestWorker *workerp;
        int32_t code;
        void *valuep;

        printf("Started receiver thread=%p with pool=%p\n", this, _testPoolp);
        _lock.take();
        while(1) {
            while (_count == 0) {
                if (_allStarted) {
                    /* nothing more to do */
                    break;
                }

                /* here count is 0, but there may be more coming to wait for, or 
                 * perhaps none.  Wait until we know more, so we don't do a joinAny
                 * without any more threads being created.
                 */
                _countCv.wait();
            }

            if (_count == 0)
                break;

            /* we know that count > 0, so we can wait for a worker to complete */
            _lock.release();
            code = _testPoolp->joinAny((ThreadPool::Worker **) &workerp, &valuep);
            _lock.take();

            if (code == ThreadPool::TP_ERR_ALL_DONE)
                break;

            printf("Join done for worker=%p %d %d\n", this, workerp->_inValue, workerp->_outValue);

            workerp->tpFinished();

            /* now we have one fewer threads to go */
            _count--;
        }
        _lock.release();
        return NULL;
    }
};

int
main(int argc, char **argv)
{
    ThreadPool testPool;
    TestWorkerFactory testFactory;
    TestWorker *workerp;
    uint32_t count = 1000;
    TestReceiver receiver;
    uint32_t i;

    ThreadDispatcher::setup(2);
    ThreadTimer::init();

    testPool.init(/* nthreads */ 8, &testFactory);

    if (argc < 2) {
        printf("usage: threadpooltest <count=%d> [-x (don't print help msg)]\n", count);
        return -1;
    }

    if (strcmp(argv[1], "-x") != 0) {
        count = atoi(argv[1]);
    }

    printf("Doing %d iterations\n", count);

    receiver.setJoinable();
    receiver.init(&testPool);

    for(i=0;i<count;i++) {
        testPool.get((ThreadPool::Worker **) &workerp);     /* ignore error code if wait is set */
        workerp->setParms("Hi %d %d\n", i);
        if (random() & 1) {
            workerp->tpIdleOnExit();
        }
        else {
            receiver.oneMoreStarted();
        }
        workerp->tpResume();
    }

    /* mark that we're not starting any more threads */
    receiver.allStarted();

    receiver.join(NULL);
    printf("All done\n");
}
