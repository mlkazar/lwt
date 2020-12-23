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
 * You start by creating an EpollOne object, which has an associated
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
 * The epoll system uses level-triggered events.  When you call wait,
 * the event is reenabled, and the wait call returns if the event is
 * ready.  Once wait returns, the event is disabled, and won't show up
 * in the epoll results, until wait is called again.
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

/* create one of these for each pthread you want doing the monitoring */
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
    dqueue<EpollEvent> _addingEvents;

    EpollOne();

    static void *threadStart(void *p);

    void close();               /* does reference count release as well */

    void wakeThreadNL();

    void init(EpollSys *sysp);
};

class EpollSys {
    friend class EpollOne;
    friend class EpollEvent;

    uint32_t _refCount;
    ThreadMutex _lock;

    EpollOne _readOne;
    EpollOne _writeOne;

 public:
    EpollSys() {
        _readOne.init(this);
        _writeOne.init(this);
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

/* reference counting works as follows: users keep refcount to EpollEvent, and they're
 * freed whenever the count hits zero.  User keeps a reference to the EpollOne as well,
 * and each event also keeps a reference to the system.  The polling pthread also
 * keeps a long term reference to the system.
 *
 * When the user calls shutdown and subsequently drops their sys reference, the pthread
 * will terminate and drop its reference, and all events will trigger with a shutdown
 * indication.  Once all user references to events and sys are gone, all memory will
 * be freed.
 *
 * The way that events work is that they're disabled once they
 * trigger, until we reenable them.  The typical use is for the user
 * to call wait, and then reenable the trigger once some data has been
 * consumed.  But if someone calls wait a second time forgetting to
 * reenable, we want to reenable it automatically for them.  So, even
 * though triggered and mustReenable seem similar, they get turned off
 * at different times.
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
        inAddingQueue = 1,
        inRemovingQueue = 2,
        inActiveQueue = 3
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
    }

    void removeFromQueues() {
        EpollOne *onep = _onep;
        if (_inQueue == inAddingQueue) {
            onep->_addingEvents.remove(this);
        }
        else if (_inQueue == inRemovingQueue) {
            onep->_removingEvents.remove(this);
        }
        else if (_inQueue == inActiveQueue) {
            onep->_activeEvents.remove(this);
        }
        _inQueue = inNoQueue;
    }

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

        /* reenable or add it the first time */
        reenableNL(fl);

        while ( !_triggered && !_closed) {
            _cv.wait(&_sysp->_lock);
        }
        _triggered = 0;
        _sysp->_lock.release();
        return 0;
    }

    void closeNL() {
        removeFromQueues();
        _onep->_removingEvents.append(this);
        _inQueue = inRemovingQueue;
        _closed = 1;
        _cv.broadcast();
        /* we don't call wakeThreadNL here since this might be called
         * from the event thread itself.  If you need to, call it yourself.
         */
    }

    void close() {
        /* move the event into the removal queue, drop our reference,
         * and wakeup the event waiting thread to process the removal
         * queue and get rid of the thread's reference once we remove
         * the entry from the remove queue.
         */
        _sysp->_lock.take();
        closeNL();
        _onep->wakeThreadNL();
        _sysp->_lock.release();
    }

    /* drop a reference; this is for internal use only; owners of an event
     * should call close on it, which will move it to a queue for releasing.
     */
    void release() {
        _sysp->_lock.take();
        releaseNL();
        _sysp->_lock.release();
    };

    void releaseNL();

    void reenableNL(Flags fl);
};

#endif /*  _EPOLL_H_ENV__ */
