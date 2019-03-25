#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

#include "task.h"

long long getus()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}

class PingTask;
class PongTask;

PingTask *main_pingp;
PongTask *main_pongp;

SpinLock main_lock;
int main_pingWaiting;
int main_pongWaiting;
long main_counter;
long main_maxCount;

class PingTask : public Task
{
public:
    SpinLock *_lockp;
    PongTask *_pongTaskp;
    void start();

    void setParms(PongTask *pongTaskp, SpinLock *lockp) {
        _pongTaskp = pongTaskp;
        _lockp = lockp;
    }
};

class PongTask : public Task
{
public:
    SpinLock *_lockp;
    PingTask *_pingTaskp;
    void start();

    void setParms(PingTask *pingTaskp, SpinLock *lockp) {
        _pingTaskp = pingTaskp;
        _lockp = lockp;
    }
};

class PingPong {
    PingTask *_pingTaskp;
    PongTask *_pongTaskp;
    SpinLock _lock;

public:
    void init() {
        _pingTaskp = new PingTask();
        _pongTaskp = new PongTask();
        _pingTaskp->setParms(_pongTaskp, &_lock);
        _pongTaskp->setParms(_pingTaskp, &_lock);
        
        /* now start the ping task */
        _pingTaskp->queue();
    };
};

void
PingTask::start() {
    TaskDispatcher *disp = TaskDispatcher::currentDispatcher();
    long long startUs;

    printf("ping starts\n");
    startUs = getus();
    while(1) {
        _lockp->take();
        if (main_counter++ > main_maxCount) {
            printf("%d task round trips, %d ns each\n",
                   main_maxCount, (getus() - startUs) * 1000 / main_maxCount);
            printf("Done!\n");
            return;
        }
        _pongTaskp->queue();
        TaskDispatcher::sleep(_lockp);
    }
}

void
PongTask::start() {
    TaskDispatcher *disp = TaskDispatcher::currentDispatcher();
    printf("pong starts\n");
    while(1) {
        _lockp->take();
        _pingTaskp->queue();
        TaskDispatcher::sleep(_lockp);
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
    printf("%d lock/unlock pairs %d ns each\n",
           main_maxCount, (getus() - startUs) * 1000 / main_maxCount);

    /* start the dispatcher */
    TaskDispatcher::setup(/* # of pthreads */ 2);

    /* start task on a dispatcher */
    for(i=0;i<4;i++) {
        pingPongp = new PingPong();
        pingPongp->init();
    }

    while(1) {
        sleep(2);
    }
    return 0;
}
