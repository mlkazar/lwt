#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "thread.h"
#include "threadmutex.h"
#include "threadtimer.h"

class TimerTest;

long main_counter;
long main_maxCount;

void delayRand(long maxSpins) {
    long i;
    long spins;
    static long total = 0;

    spins = random() % maxSpins;
    for(i=0;i<spins;i++) 
        total++;
};

class TimerTest : public Thread {
public:
    ThreadTimer *_timerp;
    static ThreadMutex _lock;
    ThreadCond _cv;

    static void timerFired(ThreadTimer *timerp, void *contextp) {
        TimerTest *testp;
        _lock.take();
        if (timerp->isCanceled()) {
            _lock.release();
            return;
        }
        testp = (TimerTest *) contextp;
        testp->_cv.broadcast();
        _lock.release();
    }

    void *start() {
        _lock.take();
        while(1) {
            _timerp = new ThreadTimer(1000, timerFired, this);
            _timerp->start();
            _cv.wait(&_lock);
            printf("back from sleep\n");
        }
    }
};

/* statics */
ThreadMutex TimerTest::_lock;

int
main(int argc, char **argv)
{
    long long us;
    int code;
    TimerTest *testp;
    
    if (argc<2) {
        printf("usage: timertest <count>\n");
        return -1;
    }
    
    main_maxCount = atoi(argv[1]);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);

    ThreadTimer::init();

    testp = new TimerTest();
    testp->queue();

    while(1) {
        sleep(100);
    }
    return 0;
}

