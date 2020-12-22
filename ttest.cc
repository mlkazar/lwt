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
#include <string>

#include "thread.h"
#include "threadmutex.h"

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
long main_doneCounter;

class PingThread : public Thread
{
public:
    SpinLock *_lockp;
    PongThread *_pongThreadp;
    void *start();

    PingThread() : Thread("Ping") {
        return;
    };

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

    PongThread() : Thread("Pong") {
        return;
    }

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
            main_doneCounter++;
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

class CreateSleep : public Thread {
public:
    ThreadMutex _lock;
    ThreadCond _cv;
    class Child : public Thread {
    public:
        ThreadMutex *_mutexp;
        ThreadCond *_cvp;
        Child(ThreadMutex *mutexp, ThreadCond *cvp) {
            _mutexp = mutexp;
            _cvp = cvp;
        }

        void *start() {
            _mutexp->take();
            _cvp->broadcast();
            _mutexp->release();
        }
    };

    CreateSleep() {
        _cv.setMutex(&_lock);
    }

    void *start() {
        Child *childp;
        childp = new Child(&_lock, &_cv);

        _lock.take();
        childp->queue();
        _cv.wait();
        _lock.release();
    }
};

int
main(int argc, char **argv)
{
    long i;
    SpinLock tlock;
    long long startUs;
    PingPong *pingPongp;
    CreateSleep *csleep;
    void *junkp;
    static const int pingCount = 10;
    
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
    main_doneCounter = 0;
    for(i=0;i<pingCount;i++) {
        pingPongp = new PingPong();
        pingPongp->init();
    }

    while(1) {
        if (main_doneCounter >= pingCount) {
            printf("All done\n");
            break;
        }
        sleep(1);
    }

    printf("Starting timing test for thread create/delete pairs + joins\n");
    startUs = getus();
    for(i=0;i<main_maxCount;i++) {
        csleep = new CreateSleep();
        csleep->setJoinable();
        csleep->queue();
        csleep->join(&junkp);
    }
    printf("%d thread create/deletes %ld ns each\n",
           (int) main_maxCount, (long) (getus() - startUs) * 1000 / main_maxCount);

    _exit(0);
    return 0;
}
