#ifndef _EPOLL_H_ENV__
#define _EPOLL_H_ENV__ 1

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

/* This module provides an async interface for waiting for file
 * descriptors to be ready for reading, writing or accepting
 * connections.  The file descriptor doesn't have to be in
 * non-blocking mode.
 *
 * Internall, we start by creating an EpollOne object for the read
 * side and the write side, each of which which has an associated
 * pthread that does the actual waiting.  Then you create a new
 * EpollEvent for a file descriptor, and you call the wait method with
 * either epollIn or epollOut, to wait for either input to be
 * available, or for at least *some* room for output to be available.
 * You wait for input if you're waiting for a new connection from a
 * listening socket.
 *
 * While waiting, the Thread is sleeping, but the dispatcher pthread
 * runs other Threads while waiting.
 *
 * The epoll system uses level-triggered events, to eliminate the risk
 * of missing an event.  These events get disabled after firing, and
 * reenabled just before waiting again.  When you call wait, the event
 * is reenabled, and the wait call returns if the event is ready.
 * Once wait returns, the event is disabled, and won't show up in the
 * epoll results, until wait is called again.
 *
 * When done with an epoll event, you call close on the event object;
 * this will release its reference and free the underlying storage.
 *
 * Similarly, after releasing any events from the epollSys, you can
 * close the epollSys object and its storage will eventually be freed.
 */
#include <sys/epoll.h>
#include <pthread.h>

#include "dqueue.h"
#include "thread.h"
#include "threadmutex.h"

class EpollOne;
class EpollEvent;
class EpollSys;

/* internal: creates one of these for each pthread doing the monitoring */
class EpollOne {
    friend class EpollEvent;
 public:
    EpollSys *_sysp;
    pthread_t _pthread;    /* managing thread */ 
    int _epFd;
    int _readWakeupFd;
    int _writeWakeupFd;
    uint8_t _pthreadActive;
    uint8_t _doShutdown;
    uint8_t _setChanged;
    uint8_t _specialEventWakeup;  /* addresse used to distinguish special event */
    uint8_t _running;
    dqueue<EpollEvent> _activeEvents;
    dqueue<EpollEvent> _removingEvents;

    EpollOne();

    static void *threadStart(void *p);

    void close();               /* does reference count release as well */

    void wakeThreadNL();

    void init(EpollSys *sysp, const char* name);
};

class EpollSys {
    friend class EpollOne;
    friend class EpollEvent;

    uint32_t _refCount;
    ThreadMutex _lock;

    EpollOne _readOne;
    EpollOne _writeOne;

 public:
    EpollSys(const char *name = NULL) {
        _refCount = 1;
        char thr_name[32];

        snprintf(thr_name, sizeof(thr_name), "%s:rp", name ? name : "unk");
        _readOne.init(this,thr_name);
        snprintf(thr_name, sizeof(thr_name), "%s:wp", name ? name : "unk");
        _writeOne.init(this,thr_name);
    }

    void hold() {
        _lock.take();
        _refCount++;
        _lock.release();
    }

    void releaseNL();

    /* not sure this is useful */
    void release() {
        _lock.take();
        releaseNL();
        _lock.release();
    }

    void close() {
        _readOne.close();
        _writeOne.close();
    }
};

/* reference counting works as follows: users keep refcount to
 * EpollEvent, and they're freed whenever the count hits zero.  User
 * keeps a reference to the EpollSys as well, and each event also
 * keeps a reference to the system.  The polling pthread also keeps a
 * long term reference to the system.
 *
 * When the user calls shutdown and subsequently drops their sys
 * reference, the pthread will terminate and drop its reference, and
 * all events will trigger with a shutdown indication.  Once all user
 * references to events and sys are gone, all memory will be freed.
 *
 * The way that events work is that they're disabled once they
 * trigger, until we reenable them.  The typical use is for the user
 * to call wait, then do something with the file descriptor and then
 * go back to calling wait.  When wait is called with a disabled
 * event, the event is reenabled.
 *
 * Because EpollEvents are loaded into the kernel and may be returned
 * by an epoll call, when closing an epoll event, we bump the
 * reference count an extra time and put the event into a removing
 * queue.  The event will be eventually released by the pthread when
 * it is *not* in an epoll call, thus ensuring we won't be surprised
 * by the epoll event's address showing up from the kernel.  The last
 * reference from the event won't be released until after the event
 * has been removed by this pthread.
 */
class EpollEvent {
    friend class EpollOne;
    friend class EpollSys;

 public:
    /* these are bitmasks */
    enum Flags {
        epollIn = 1,
        epollOut = 2
    };

    enum QueueId {
        inNoQueue = 0,
        inActiveQueue = 1,
        inRemovingQueue = 2,
    };

 private:
    int _fd;
    uint32_t _refCount;
    uint8_t _triggered;
    uint8_t _added;             /* first time must call add instead */
    uint8_t _failed;            /* for debugging, tells us event couldn't be added */
    uint8_t _closed;
    uint8_t _isWrite;
    Flags _flags;               /* flags of last type we waited for */

    /* which queue are we in */
    QueueId _inQueue;

 public:
    EpollEvent *_dqNextp;
    EpollEvent *_dqPrevp;
 private:

    EpollOne *_onep;
    EpollSys *_sysp;
    ThreadCond _cv;

 public:
    EpollEvent(EpollSys *sysp, int fd, int isWrite) {
        _fd = fd;
        _refCount = 1;
        _triggered = 0;
        _added = 0;
        _isWrite = isWrite;
        _failed = 0;
        _flags = (Flags) 0;
        _sysp = sysp;
        _onep = (isWrite? &sysp->_writeOne : &sysp->_readOne);
        _sysp->hold();
        _cv.setMutex(&_sysp->_lock);
        _dqPrevp = NULL;
        _dqNextp = NULL;
        _inQueue = inNoQueue;
        _closed = 0;

        sysp->_lock.take();
        sysp->_refCount++;
        sysp->_lock.release();
    }

    void removeFromQueues() {
        EpollOne *onep = _onep;
        if (_inQueue == inRemovingQueue) {
            onep->_removingEvents.remove(this);
        }
        else if (_inQueue == inActiveQueue) {
            onep->_activeEvents.remove(this);
        }
        _inQueue = inNoQueue;
    }

    /* note that this returns immediately if the event has been closed */
    int32_t wait(Flags fl) {
        _sysp->_lock.take();

        /* if this event has already been triggered, turn off the indicator
         * and return.
         */
        if (_triggered) {
            _triggered = 0;
            _sysp->_lock.release();
            return 0;
        }

        while ( !_triggered && !_closed) {
            /* reenable or add it the first time; we do this each time we
             * block, in case another thread turned off triggered
             */
            reenableNL(fl);

            _cv.wait(&_sysp->_lock);
        }
        _triggered = 0;
        _sysp->_lock.release();
        return (_closed? -1 : 0);
    }

    void hold() {
        EpollSys *sysp = _sysp;

        sysp->_lock.take();
        holdNL();
        sysp->_lock.release();
    }

    void holdNL() {
        _refCount++;
    }

    void closeNL();

    void close() {
        EpollSys *sysp = _sysp;
        EpollOne *onep = _onep;

        /* move the event into the removal queue, drop our reference,
         * and wakeup the event waiting thread to process the removal
         * queue and get rid of the thread's reference once we remove
         * the entry from the remove queue.
         *
         * Note that as soon as we've called closeNL, the event might
         * be released and freed, so pull out fields we need from it
         * first.
         */
        sysp->_lock.take();
        closeNL();
        onep->wakeThreadNL();
        sysp->_lock.release();
    }

    /* drop a reference; this is for internal use only; owners of an event
     * should call close on it, which will move it to a queue for releasing.
     */
    void release() {
        EpollSys *sysp = _sysp;

        sysp->_lock.take();
        releaseNL();
        sysp->_lock.release();
    };

    void releaseNL();

    void reenableNL(Flags fl);
};

#endif /*  _EPOLL_H_ENV__ */
