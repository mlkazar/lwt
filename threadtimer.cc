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

#include <poll.h>
#include "threadtimer.h"

/* stupid rabbit */
pthread_mutex_t ThreadTimer::_timerMutex;
pthread_cond_t ThreadTimer::_timerCond;
int ThreadTimer::_noticeFds[2];
int ThreadTimer::_threadRunning;
dqueue<ThreadTimer> ThreadTimer::_allTimers;
int ThreadTimer::_didInit= 0;
ThreadMutex ThreadCondTimed::_internalLock;

/* static */ void
ThreadTimer::init()
{
    pthread_t junkId;

    /* must call this to setup fake dispatcher structures so that this thread can
     * can thread blocking functions from the user's callback function.
     */
    ThreadDispatcher::pthreadTop();
    
    ::pipe(_noticeFds);

    pthread_mutex_init(&_timerMutex, NULL);
    pthread_cond_init(&_timerCond, NULL);

    _threadRunning = 1;

    pthread_create(&junkId, NULL, &ThreadTimer::timerManager, NULL);

    _didInit = 1;
}

void *
ThreadTimer::timerManager(void *parmp)
{
    ThreadTimer *timerp;
    struct pollfd pipeReadEvent;
    int32_t code;
    unsigned long long now;
    long long sleepMs;

    ThreadDispatcher::pthreadTop();

    pipeReadEvent.fd = _noticeFds[0];
    pipeReadEvent.events = POLLIN;

    pthread_mutex_lock(&_timerMutex);
    while(1) {
        while(1) {
            /* process all expired timers */
            now = osp_getMs();
            timerp = _allTimers.head();
            if (!timerp) {
                sleepMs = -1;
                break;
            }

            assert(timerp->_inQueue);
            if (now >= timerp->_expiration) {
                _allTimers.remove(timerp);
                timerp->_inQueue = 0;
                timerp->hold();
                pthread_mutex_unlock(&_timerMutex);
                timerp->_callbackp(timerp, timerp->_contextp);
                pthread_mutex_lock(&_timerMutex);
                if (!timerp->_canceled) {
                    timerp->_canceled = 1;
                    timerp->release();  /* from original creation, returning w/refCount==1 */
                }
                timerp->release();      /* from hold above */
            }
            else {
                /* this is the next timer to expire; sleep until then */
                sleepMs = timerp->_expiration - now;
                break;
            }
        } /* loop over unexpired timers */

        _threadRunning = 0;
        pthread_mutex_unlock(&_timerMutex);
            
        pipeReadEvent.revents = 0;
        code = poll(&pipeReadEvent, 1, sleepMs);
        if (code < 0) {
            if (errno != EINTR) {
                perror("timerThreads poll");
                assert(0);
            }
        }
        else if (code > 0) {
            /* this is a wakeup event, so read the pipe */
            char junk;
            ::read( _noticeFds[0], &junk, 1);
        }

        pthread_mutex_lock(&_timerMutex);
        _threadRunning = 1;
    }
}

void
ThreadTimer::start()
{
    ThreadTimer *ttp;

    assert(_didInit);

    _expiration = osp_getMs() + _msecs;
    pthread_mutex_lock(&_timerMutex);
    if (_inQueue) {
        _allTimers.remove(this);
    }

    _inQueue = 1;
    /* sorted insert, keeping timer list sorted by increasing expiration time */
    for(ttp = _allTimers.tail(); ttp; ttp=ttp->_dqPrevp) {
        if (ttp->_expiration <= _expiration)
            break;
    }
    _allTimers.insertAfter(ttp, this);

    /* now wakeup the timer manager thread, if necessary */
    if (!_threadRunning) {
        char junk;
        junk = 'a';
        ::write(_noticeFds[1], &junk, 1);
    }

    pthread_mutex_unlock(&_timerMutex);
}

/* you can't cancel a timer after you've returned from the callback, since it will
 * be free then.  It probably should be a fatal error to call cancel when the canceled
 * flag is already set.
 */
int32_t
ThreadTimer::cancel()
{
    pthread_mutex_lock(&_timerMutex);
    if (!_canceled) {
        _canceled = 1;
        release();
    }
    pthread_mutex_unlock(&_timerMutex);
    return 0;
}

/* static */ void
ThreadTimerSleep::condWakeup(ThreadTimer *timerp, void *contextp)
{
    ThreadTimerSleep *sp = (ThreadTimerSleep *)contextp;
    sp->_mutex.take();
    sp->_cv.broadcast();
    sp->_mutex.release();
}

int32_t
ThreadTimerSleep::sleep(uint32_t ams)
{
    ThreadTimer *localTimerp;

    _ms = ams;
    localTimerp = new ThreadTimer(ams, &ThreadTimerSleep::condWakeup, this);

    _mutex.take();
    localTimerp->start();       /* automatically free when it fires */
    _cv.wait(&_mutex);
    _mutex.release();

    return 0;
}
