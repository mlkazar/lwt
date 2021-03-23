#include <stdio.h>
#include <errno.h>

#include "epoll.h"

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

EpollOne::EpollOne()
{
    int fds[2];

    _sysp = NULL;
    _epFd = epoll_create1(0);
    _pthreadActive = 1;
    _doShutdown = 0;
    _setChanged = 0;
    pipe(fds);
    _readWakeupFd = fds[0];
    _writeWakeupFd = fds[1];
    _running = 1;
}

void
EpollOne::init(EpollSys *sysp) {
    pthread_t junkId;

    _sysp = sysp;
    pthread_create(&junkId, NULL, &EpollOne::threadStart, this);
}

/* The way that shutting down this code works is first, you close all the EpollEvent
 * objects, which drops their reference counts.  When they hit zero, they drop
 * *their* reference to the EpollOne object.  When the user then calls ::close on
 * the EpollOne object, it sets a flag in the sys object, wakes up the pthread,
 * which exits immediately after dropping the original reference count returned to
 * the user when the EpollOne was created.  When the reference count goes to 0,
 * the EpollOne object is finally freed.
 */
void *
EpollOne::threadStart(void *argp)
{
    int32_t code;
    int32_t evCount;
    epoll_event ev;
    EpollEvent *ep;
    EpollEvent *nep;
    EpollOne *onep = (EpollOne *) argp;
    static const uint32_t nevents = 16;
    epoll_event localEvents[nevents];
    epoll_event *eventp;
    int32_t i;
    EpollSys *sysp = onep->_sysp;
    
    /* turn this pthread into a dispatcher for a dedicated thread, so we can
     * wait for thread locks correctly.
     */
    ThreadDispatcher::pthreadTop("Epoll");

    onep->_pthread = pthread_self();

    ev.events = EPOLLIN;
    ev.data.ptr = &onep->_specialEventWakeup;
    code = epoll_ctl(onep->_epFd, EPOLL_CTL_ADD, onep->_readWakeupFd, &ev);
    if (code < 0) {
        printf("epoll: add failed for initial pipe\n");
        osp_assert(0);
    }

    while(1) {
        /* collect updates */
        sysp->_lock.take();

        for(ep = onep->_removingEvents.head(); ep; ep = nep) {
            nep = ep->_dqNextp;
            /* closing the file descriptor removes it from the epoll set,
             * and the event was put in this queue by close
             */
            onep->_removingEvents.remove(ep);
            ep->_inQueue = EpollEvent::inRemovingQueue;

            /* this releases the reference from the close operation;
             * we only go through this path after the user calls
             * ::close on the EpollEvent, which resets the event and
             * then triggers the removal of the event from the set of
             * epoll events.
             */
            ep->releaseNL();
        }

        onep->_running = 0;

        sysp->_lock.release();
        
        /* do the wait */
        evCount = epoll_wait(onep->_epFd, localEvents, nevents, -1);
        if (evCount < 0) {
            if (errno == EINTR)
                continue;
            osp_assert("epoll_wait failed" == 0);
        }

        sysp->_lock.take();
        onep->_running = 0;
        for( i=0, eventp = localEvents;
             i<evCount;
             i++, eventp++) {
            ep = (EpollEvent *) eventp->data.ptr;
            if ((uint8_t *) ep != &onep->_specialEventWakeup) {
                ep->_triggered = 1;
                ep->_cv.broadcast();
            }
            else {
                /* read from the file descriptor */
                uint8_t tc;
                code = read(onep->_readWakeupFd, &tc, 1);
            }
        }
        sysp->_lock.release();
    }
}

void
EpollSys::releaseNL()
{
    osp_assert(_refCount > 0);
    if (--_refCount == 0) {
        delete this;
    }
}

#if 0
int32_t
EpollSys::addEvent(EpollEvent *ep) {
    int32_t code;

    if (ep->_flags & EpollEvent::epollIn)
        code = _readOne.addEvent(ep);
    else
        code = _writeOne.addEvent(ep);

    return code;
}
#endif

void
EpollOne::wakeThreadNL()
{
    uint8_t tc;

    if (!_running) {
        _running = 1;
        tc = 'x';
        write(_writeWakeupFd, &tc, 1);
    }
}

void
EpollOne::close()
{
    EpollEvent *ep;
    EpollEvent *nep;

    _sysp->_lock.take();

    for(ep=_activeEvents.head(); ep; ep=nep) {
        nep = ep->_dqNextp;
        _activeEvents.remove(ep);
        _removingEvents.append(ep);
        ep->_inQueue = EpollEvent::inRemovingQueue;
    }
    wakeThreadNL();

    _sysp->_lock.release();
}

void
EpollEvent::reenableNL(Flags fl)
{
    int32_t code;
    epoll_event ev;
    
    /* for debugging more than anything else, remember what we've enabled last */
    _flags = fl;

    ev.events = EPOLLONESHOT | ((_flags & EpollEvent::epollIn)? EPOLLIN : EPOLLOUT);
    ev.data.ptr = this;
    if (!_added) {
        code = epoll_ctl(_onep->_epFd, EPOLL_CTL_ADD, _fd, &ev);
        _added = 1;
    }
    else {
        code = epoll_ctl(_onep->_epFd, EPOLL_CTL_MOD, _fd, &ev);
    }
    if (code < 0) {
        perror("reenableNL");
    }
    _onep->wakeThreadNL();
}

void
EpollEvent::releaseNL()
{
    osp_assert(_refCount > 0);
    if (--_refCount == 0) {
        _sysp->releaseNL();
        _sysp = NULL;
        delete this;
    }
}

/* called with system lock held */
void
EpollEvent::closeNL() {
    epoll_event ev;

    if (_closed)
        return;

    epoll_ctl(_onep->_epFd, EPOLL_CTL_DEL, _fd, &ev);

    removeFromQueues();
    _onep->_removingEvents.append(this);
    _inQueue = inRemovingQueue;
    _closed = 1;
    _cv.broadcast();
    /* we don't call wakeThreadNL here since this might be called
     * from the event thread itself.  If you need to, call it yourself.
     */
}
