#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <assert.h>

#include "thread.h"

long long getus()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}

class PingThread;
class PongThread;

PingThread *main_pingp;
PongThread *main_pongp;

SpinLock main_lock;
int main_pingWaiting;
int main_pongWaiting;
long main_counter;
long main_maxCount;

class PingThread : public Thread
{
public:
    SpinLock *_lockp;
    PongThread *_pongThreadp;
    void *start();

    void setParms(PongThread *pongThreadp, SpinLock *lockp) {
        _pongThreadp = pongThreadp;
        _lockp = lockp;
    }
};

class PongThread : public Thread
{
public:
    SpinLock *_lockp;
    PingThread *_pingThreadp;
    void *start();

    void setParms(PingThread *pingThreadp, SpinLock *lockp) {
        _pingThreadp = pingThreadp;
        _lockp = lockp;
    }
};

class PingPong {
    PingThread *_pingThreadp;
    PongThread *_pongThreadp;
    SpinLock _lock;

public:
    void init() {
        _pingThreadp = new PingThread();
        _pongThreadp = new PongThread();
        _pingThreadp->setParms(_pongThreadp, &_lock);
        _pongThreadp->setParms(_pingThreadp, &_lock);
        
        /* now start the ping thread */
        _pingThreadp->queue();
    };
};

void *
PingThread::start() {
    long long startUs;

    printf("ping starts\n");
    startUs = getus();
    while(1) {
        _lockp->take();
        if (main_counter++ > main_maxCount) {
            printf("%d thread round trips, %ld ns each\n",
                   (int) main_maxCount, (long) (getus() - startUs) * 1000 / main_maxCount);
            printf("Done!\n");
            return NULL;
        }
        _pongThreadp->queue();
        Thread::sleep(_lockp);
    }
}

void *
PongThread::start() {
    printf("pong starts\n");
    while(1) {
        _lockp->take();
        _pingThreadp->queue();
        Thread::sleep(_lockp);
        assert(this == Thread::getCurrent());
    }
}

int
main(int argc, char **argv)
{
    long i;
    SpinLock tlock;
    long long startUs;
    PingPong *pingPongp;
    
    if (argc<2) {
        printf("usage: ttest <count>\n");
        return -1;
    }
    
    main_maxCount = atoi(argv[1]);

    startUs = getus();
    for(i=0;i<main_maxCount;i++) {
        tlock.take();
        tlock.release();
    }
    printf("%d lock/unlock pairs %ld ns each\n",
           (int) main_maxCount, (long) (getus() - startUs) * 1000 / main_maxCount);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);

    /* start thread on a dispatcher */
    for(i=0;i<8;i++) {
        pingPongp = new PingPong();
        pingPongp->init();
    }

    while(1) {
        sleep(2);
    }
    return 0;
}
