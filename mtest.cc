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

long main_counter;
long main_maxCount;
Deadlock *main_deadlockp;

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

    static void delayRand(long maxSpins) {
        long i;
        long spins;
        static long total = 0;

        spins = random() % maxSpins;
        for(i=0;i<spins;i++) 
            total++;
    };

    class ABThread : public Thread {
    public:
        Deadlock *_deadlockp;
        
        ABThread(Deadlock *deadlockp) {
            _deadlockp = deadlockp;
        }

        void start();
    };

    class BAThread : public Thread {
    public:
        Deadlock *_deadlockp;
        
        BAThread(Deadlock *deadlockp) {
            _deadlockp = deadlockp;
        }

        void start();
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

    void start() {
        int i;

        for(i=0;i<main_maxCount;i++) {
            _absp[i]->queue();
            _basp[i]->queue();
        }
    };

    long long getSpins() {
        return _bigSpins;
    };
    
};

void
Deadlock::BAThread::start() 
{
    int spins;
    int i;
            
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

void
Deadlock::ABThread::start() 
{
    int spins;
    int i;
            
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

    void start();

    void setParms(PingPong *pingPongp) {
        _pingThreadp = this;
        _pp = pingPongp;
    }
};

class PongThread : public Thread {
public:
    PingPong *_pp;
    PongThread *_pongThreadp;

    void start();

    void setParms(PingPong *pingPongp) {
        _pongThreadp = this;
        _pp = pingPongp;
    }
};

void
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
            printf("%d thread round trips, %lld ns each\n",
                   main_maxCount, (osp_getUs() - startUs) * 1000 / main_maxCount);
            printf("%lld microseconds lock wait total\n", _pp->_mutex.getWaitUs());
            printf("Done!\n");
            _pp->_mutex.release();
            return;
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

void
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
    for(i=0; i<8; i++) {
        pingPongp = new PingPong();
        pingPongp->init();
    }
}

int
main(int argc, char **argv)
{
    long long us;
    int code;
    
    if (argc<2) {
        printf("usage: ttest <testname> <count>\n");
        printf("usage: testname = {basic,deadlock}\n");
        return -1;
    }
    
    main_maxCount = atoi(argv[2]);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);

    if (strcmp(argv[1], "basic") == 0)
        basic();
    else if (strcmp(argv[1], "deadlock") == 0) {
        deadlock();
    }
    else {
        printf("unknown test '%s'\n", argv[1]);
        return -1;
    }

    while(1) {
        ThreadMutexDetect detect;
        sleep(2);
        code = detect.checkForDeadlocks();
        if (code)
            printf("main: deadlocks found\n\n");
        else
            printf("main: done with deadlock check; none found\n");
    }
    return 0;
}

