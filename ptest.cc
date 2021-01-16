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

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "thread.h"
#include "threadmutex.h"

class PingThread;
class PongThread;
class PingPong;
class Deadlock;
class Join;
class JoinA;
class JoinB;

long main_counter;
long main_maxCount;
Deadlock *main_deadlockp;
Join *main_joinp;

void delayRand(long maxSpins) {
    long i;
    long spins;
    static long total = 0;

    spins = random() % maxSpins;
    for(i=0;i<spins;i++) 
        total++;
};

class Join : public Thread {
public:
    static const uint32_t _maxThreads = 4;
    JoinA *_joinAsp[_maxThreads];
    JoinB *_joinBsp[_maxThreads];
    uint32_t _count;

    Join() : Thread("Join") {
        _count = 0;
    }

    void *start();
};

/* These threads are created by a JoinB task, and then this thread just waits
 * a random amount of time, and then exits.
 */
class JoinA : public Thread {
public:
    Join *_joinp;
    uint32_t _ix;

    JoinA() : Thread("JoinA") {};

    void init(Join *joinp, uint32_t ix) {
        _ix = ix;
        _joinp = joinp;
    }

    void *start();
};

/* These threads are created by the Join test, and they repeatedly create
 * JoinA tasks, wait a random amount of time and then attempt to join their
 * JoinA task.  They verify that the right exit value was returned.
 *
 * After the prescribed number of iterations, the JoinB task exits.
 */
class JoinB : public Thread {
public:
    Join *_joinp;
    uint32_t _ix;

    JoinB() : Thread("JoinB") {};

    void init(Join *joinp, uint32_t ix) {
        _ix = ix;
        _joinp = joinp;
    }

    void *start();
};

void *
JoinA::start()
{
    delayRand(100000);
    Thread::exit(_joinp + _ix);
    return NULL;
}

void *
Join::start() {
    uint32_t i;
    printf("Join test; should see 4 threads exit\n");
    for(i=0;i<_maxThreads;i++) {
        _joinAsp[i] = NULL;
        _joinBsp[i] = new JoinB();
        _joinBsp[i]->init(this, i);
        _joinBsp[i]->setJoinable();
        _joinBsp[i]->queue();
    }

    /* now wait for all the JoinB threads to finish */
    for(i=0;i<_maxThreads;i++) {
        _joinBsp[i]->join(NULL);
    }
    printf("All threads finished for join test\n");
    _exit(0);

    /* not reached */
    return NULL;
}

void *
JoinB::start()
{
    JoinA *jap;
    void *joinValuep;

    while(1) {
        /* create a task who will delay a while and then exit */
        jap = new JoinA();
        _joinp->_joinAsp[_ix] = jap;
        jap->init(_joinp, _ix);
        jap->setJoinable();
        jap->queue();

        /* now delay a random amount, and do the join */
        delayRand(100000);

        jap->join(&joinValuep);
        assert(joinValuep = _joinp + _ix);
        delete jap;

        _joinp->_count++;
        if (_joinp->_count > main_maxCount) {
            printf("Join test: ran %d iterations\n", _joinp->_count);
            Thread::exit(NULL);
        }

        /* otherwise, we keep going.  Note that the join call above actually
         * freed the JoinA thread, so we just recreate one at the top of
         * the loop.
         */
        _joinp->_joinAsp[_ix] = NULL;
    }
}

class Deadlock {
public:

    class ABThread;
    class BAThread;

    static const int _maxThreads = 4;

    ThreadMutex _mutexA;
    ThreadMutex _mutexB;
    long long _bigSpins;

    Thread *_absp[_maxThreads];
    Thread *_basp[_maxThreads];

    class ABThread : public Thread {
    public:
        Deadlock *_deadlockp;
        
        ABThread(Deadlock *deadlockp) {
            _deadlockp = deadlockp;
        }

        void *start();
    };

    class BAThread : public Thread {
    public:
        Deadlock *_deadlockp;
        
        BAThread(Deadlock *deadlockp) {
            _deadlockp = deadlockp;
        }

        void *start();
    };


    Deadlock() {
        printf("Deadlock structure at %p\n", this);
        int i;
        if ( main_maxCount > _maxThreads)
            main_maxCount = _maxThreads;

        _bigSpins = 0;

        for(i=0; i<main_maxCount; i++) {
            _absp[i] = new ABThread(this);
            _basp[i] = new BAThread(this);
        }
    };

    void *start() {
        int i;

        for(i=0;i<main_maxCount;i++) {
            _absp[i]->queue();
            _basp[i]->queue();
        }
        return NULL;
    };

    long long getSpins() {
        return _bigSpins;
    };
    
};

void *
Deadlock::BAThread::start() 
{
    while(1) {
        _deadlockp->_mutexB.take();
        _deadlockp->_mutexA.take();
        delayRand(1000000);
        _deadlockp->_mutexA.release();
        _deadlockp->_mutexB.release();
        
        delayRand(10000);

        _deadlockp->_bigSpins++;
    }
}

void *
Deadlock::ABThread::start() 
{
    while(1) {
        _deadlockp->_mutexA.take();
        _deadlockp->_mutexB.take();
        delayRand(1000000);
        _deadlockp->_mutexB.release();
        _deadlockp->_mutexA.release();
        delayRand(10000);

        _deadlockp->_bigSpins++;
    }
}

/* this class is used to test condition variables and mutexes.  There
 * is a small buffer of integers.  Ping looks for zero entries in the
 * buffer array and fills them in.  As soon as it fills in at least
 * one entry, it drops the mutex and wakes up anyone waiting to empty
 * the buffers (the pong threads).  Each time a buffer is emptied, we
 * wake up the thread filling buffers (the ping thread)
 *
 * The ping task keeps a running count of all the integers it has sent the 
 * pong task in pingTotal.  The pong task keeps a total of all the numbers it
 * has read out of the buffers in pongTotal.  Each time the pong task runs, it checks
 * that pingTotal = pongTotal + <whatever it finds in the buffers>.  This should always
 * be true when the mutex is held.
 */
class PingPong {
public:
    static const int _maxBuffers = 3;
    void *_pingThreadp;
    void *_pongThreadp;
    pthread_mutex_t _mutex;
    pthread_cond_t _needSpaceCV;
    pthread_cond_t _needDataCV;
    int _haveData;
    long long _pingTotal;
    long long _pongTotal;

    PingPong() {
        _pingTotal = 0;
        _pongTotal = 0;

        _haveData = 0;

        pthread_mutex_init(&_mutex, NULL);
        pthread_cond_init(&_needSpaceCV, NULL);
        pthread_cond_init(&_needDataCV, NULL);
    }

    void init();
};

class PingThread {
public:
    static void *start(void *arg);
};

class PongThread {
public:
    static void *start(void *arg);
};

void *
PingThread::start(void *argp) {
    long long startUs;
    PingPong *pp = (PingPong *)argp;

    printf("ping starts\n");
    startUs = osp_getUs();
    pthread_mutex_lock(&pp->_mutex);
    while(1) {
        /* supply data into the buffers */

        /* count how many spins */
        if (main_counter++ > main_maxCount) {
            printf("%d thread round trips, %ld ns each\n",
                   (int) main_maxCount, (long) (osp_getUs() - startUs) * 1000 / main_maxCount);
            printf("Done!\n");
            pthread_mutex_unlock(&pp->_mutex);
            return NULL;
        }

        while(pp->_haveData) {
            /* wait for space in the array */
            pthread_cond_wait(&pp->_needSpaceCV, &pp->_mutex);
        }

        pp->_haveData = 1;
        pthread_cond_broadcast(&pp->_needDataCV);
    }
}

void *
PongThread::start(void *argp) {
    PingPong *pp = (PingPong *) argp;
    printf("pong starts\n");
    
    /* consume */ 
    pthread_mutex_lock(&pp->_mutex);
    while(1) {
        while(!pp->_haveData) {
            pthread_cond_wait(&pp->_needDataCV, &pp->_mutex);
        }

        /* at this point we have data, so consume it */
        pp->_haveData = 0;
        pthread_cond_broadcast(&pp->_needSpaceCV);
    }
    pthread_mutex_unlock(&pp->_mutex);
}

void
PingPong::init() 
{
    pthread_t junk;
    pthread_create(&junk, NULL, &PingThread::start, this);
    pthread_create(&junk, NULL, &PongThread::start, this);
};

void
deadlock()
{
    main_deadlockp = new Deadlock();
    main_deadlockp->start();
}

class CountState {
public:
    pthread_mutex_t _lock;
    pthread_cond_t _sleepWaiter;
    pthread_cond_t _countWaiter;
    uint32_t _counter;

    CountState() {
        _counter = 0;
        pthread_mutex_init(&_lock, NULL);
        pthread_cond_init(&_countWaiter, NULL);
        pthread_cond_init(&_sleepWaiter, NULL);
    }
};

void *countProc(void *argp)
{
    CountState *sp = (CountState *) argp;

    pthread_mutex_lock(&sp->_lock);

    sp->_counter++;
    pthread_cond_broadcast(&sp->_countWaiter);

    pthread_cond_wait(&sp->_sleepWaiter, &sp->_lock);

    pthread_mutex_unlock(&sp->_lock);
    return NULL;
}

void
count()
{
    pthread_t junk;
    uint32_t i;
    CountState countState;
    int32_t code;

    for(i=0;i<main_maxCount;i++) {
        code = pthread_create(&junk, NULL, &countProc, &countState);
        if (code != 0) {
            perror("thread create");
            printf("failed after %d creates\n", i);
            exit(0);
        }
    }
    pthread_mutex_lock(&countState._lock);
    while(countState._counter < main_maxCount) {
        pthread_cond_wait(&countState._countWaiter, &countState._lock);
    }
    pthread_mutex_unlock(&countState._lock);
    printf("Counter done\n");
    exit(0);
}

void
basic()
{
    long i;
    PingPong *pingPongp;

    /* start thread on a dispatcher */
    printf("Warning: with basic test, process doesn't exit when done\n");
    for(i=0; i<1; i++) {
        pingPongp = new PingPong();
        pingPongp->init();
    }
}

void
join()
{
    main_joinp = new Join();
    main_joinp->queue();
}

int
main(int argc, char **argv)
{
    if (argc<2) {
        printf("usage: mtest <testname> <count>\n");
        printf("usage: testname = {basic,count}\n");
        return -1;
    }
    
    main_maxCount = atoi(argv[2]);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);

    /* start monitoring for deadlocks */
    ThreadMutexDetect::start();

    if (strcmp(argv[1], "basic") == 0)
        basic();
    else if (strcmp(argv[1], "count") == 0) {
        count();
    }
    else if (strcmp(argv[1], "deadlock") == 0) {
        deadlock();
    }
    else if (strcmp(argv[1], "join") == 0) {
        join();
    }
    else {
        printf("unknown test '%s'\n", argv[1]);
        return -1;
    }

    while(1) {
        sleep(100);
    }
    return 0;
}

