#ifndef _EPOLL_H_ENV__
#define _EPOLL_H_ENV__ 1

#include <sys/epoll.h>
#include <pthread.h>

#include "dqueue.h"
#include "thread.h"
#include "threadmutex.h"

class EpollSys;
class EpollEvent;

/* create one of these for each pthread you want doing the monitoring */
class EpollSys {
    friend class EpollEvent;
 public:
    uint32_t _refCount;
    ThreadMutex _lock;     /* protects all fields in this system and associated events */
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

    EpollSys();

    int32_t addEvent(EpollEvent *ep);

    static void *threadStart(void *p);

    void close();               /* does reference count release as well */

    void releaseNL();

    void wakeThreadNL();

    void hold() {
        _lock.take();
        _refCount++;
        _lock.release();
    }

    /* not sure this is useful */
    void release() {
        _lock.take();
        releaseNL();
        _lock.release();
    }
};

/* reference counting works as follows: users keep refcount to EpollEvent, and they're
 * freed whenever the count hits zero.  User keeps a reference to the EpollSys as well,
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
    uint8_t _active;            /* added to epoll kernel queue */
    uint8_t _failed;
    uint8_t _added;             /* first time must call add instead */
    Flags _flags;               /* flags of last type we waited for */

    /* which queue are we in */
    QueueId _inQueue;

 public:
    EpollEvent *_dqNextp;
    EpollEvent *_dqPrevp;
 private:

    EpollSys *_sysp;
    ThreadCond _cv;

 public:
    EpollEvent(EpollSys *sysp, int fd) {
        _fd = fd;
        _refCount = 1;
        _triggered = 0;
        _active = 0;
        _added = 0;
        _failed = 0;
        _flags = (Flags) 0;
        _sysp = sysp;
        _sysp->hold();
        _cv.setMutex(&sysp->_lock);
        _dqPrevp = NULL;
        _dqNextp = NULL;
        _inQueue = inNoQueue;
    }

    void removeFromQueues() {
        EpollSys *sysp = _sysp;
        if (_inQueue == inAddingQueue) {
            sysp->_addingEvents.remove(this);
        }
        else if (_inQueue == inRemovingQueue) {
            sysp->_removingEvents.remove(this);
        }
        else if (_inQueue == inActiveQueue) {
            sysp->_activeEvents.remove(this);
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

        while ( !_triggered) {
            _cv.wait(&_sysp->_lock);
        }
        _triggered = 0;
        _sysp->_lock.release();
        return 0;
    }

    void closeNL() {
        removeFromQueues();
        _sysp->_removingEvents.append(this);
        _inQueue = inRemovingQueue;
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
        _sysp->wakeThreadNL();
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

#if 0
    void reenable() {
        _sysp->_lock.take();
        reenableNL();
        _sysp->_lock.release();
    }
#endif

};

#endif /*  _EPOLL_H_ENV__ */
