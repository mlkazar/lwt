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
    PingThread *_pingThreadp;
    PongThread *_pongThreadp;
    ThreadMutex _mutex;
    ThreadCond _needSpaceCV;
    ThreadCond _needDataCV;
    long long _pingTotal;
    long long _pongTotal;
    long long _buffers[_maxBuffers];

    PingPong() {
        int i;
        _pingTotal = 0;
        _pongTotal = 0;

        for(i=0; i<_maxBuffers; i++) {
            _buffers[i] = 0;
        }

        _needSpaceCV.setMutex(&_mutex);
        _needDataCV.setMutex(&_mutex);
    }

    void init();
};

class PingThread : public Thread {
public:
    PingPong *_pp;
    PingThread *_pingThreadp;

    void *start();

    void setParms(PingPong *pingPongp) {
        _pingThreadp = this;
        _pp = pingPongp;
    }
};

class PongThread : public Thread {
public:
    PingPong *_pp;
    PongThread *_pongThreadp;

    void *start();

    void setParms(PingPong *pingPongp) {
        _pongThreadp = this;
        _pp = pingPongp;
    }
};

void *
PingThread::start() {
    long long startUs;
    int i;
    int tval;

    printf("ping starts\n");
    startUs = osp_getUs();
    while(1) {
        /* supply data into the buffers */
        _pp->_mutex.take();

        /* count how many spins */
        if (main_counter++ > main_maxCount) {
            printf("%d thread round trips, %ld ns each\n",
                   (int) main_maxCount, (long) (osp_getUs() - startUs) * 1000 / main_maxCount);
            printf("%lld microseconds lock wait total\n", _pp->_mutex.getWaitUs());
            printf("Done!\n");
            _pp->_mutex.release();
            return this;
        }

        /* look for space to supply */
        for(i=0; i<PingPong::_maxBuffers; i++) {
            if (_pp->_buffers[i] == 0) {
                tval = random() & 0xF;
                /* test will fail if we fill all buffers with 0s, since
                 * draining thread won't wake us.
                 */
                if (tval == 0)
                    tval++;
                _pp->_buffers[i] = tval;
                _pp->_pingTotal += tval;

                _pp->_needDataCV.broadcast();
                
                /* stop after some random number of buffers */
                if (random() & 1)
                    break;
            }
        }

        /* wait for space in the array */
        _pp->_needSpaceCV.wait(&_pp->_mutex);

        _pp->_mutex.release();
    }
}

void *
PongThread::start() {
    int i;
    int needWakeup;
    printf("pong starts\n");
    
    /* consume */ 
    _pp->_mutex.take();
    while(1) {
        needWakeup = 0;
        for(i=0; i<PingPong::_maxBuffers; i++) {
            if (_pp->_buffers[i] > 0) {
                _pp->_pongTotal += _pp->_buffers[i];
                _pp->_buffers[i] = 0;
                needWakeup = 1;
            }
        }

        if (needWakeup) {
            _pp->_needSpaceCV.broadcast();
        }
        
        assert(_pp->_pingTotal == _pp->_pongTotal);

        /* at this point, we've consumed all the available data */
        _pp->_needDataCV.wait(&_pp->_mutex);
    }
    _pp->_mutex.release();
}

void
PingPong::init() 
{
    _pingThreadp = new PingThread();
    _pongThreadp = new PongThread();
    _pingThreadp->setParms(this);
    _pongThreadp->setParms(this);
    
    /* now start the ping and pong threads */
    _pingThreadp->queue();
    _pongThreadp->queue();
};

void
deadlock()
{
    main_deadlockp = new Deadlock();
    main_deadlockp->start();
}

void
basic()
{
    long i;
    PingPong *pingPongp;

    /* start thread on a dispatcher */
    printf("Warning: with basic test, process doesn't exit when done\n");
    for(i=0; i<8; i++) {
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
        printf("usage: testname = {basic,deadlock,join}\n");
        return -1;
    }
    
    main_maxCount = atoi(argv[2]);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);

    /* start monitoring for deadlocks */
    ThreadMutexDetect::start();

    if (strcmp(argv[1], "basic") == 0)
        basic();
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

