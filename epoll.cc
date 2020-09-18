#include <stdio.h>
#include <errno.h>

#include "epoll.h"

EpollSys::EpollSys()
{
    pthread_t junkId;
    int fds[2];

    _refCount = 1;
    _epFd = epoll_create1(0);
    _pthreadActive = 1;
    _doShutdown = 0;
    _setChanged = 0;
    pipe(fds);
    _readWakeupFd = fds[0];
    _writeWakeupFd = fds[1];
    _running = 1;
    pthread_create(&junkId, NULL, &EpollSys::threadStart, this);
}

/* The way that shutting down this code works is first, you close all the EpollEvent
 * objects, which drops their reference counts.  When they hit zero, they drop
 * *their* reference to the EpollSys object.  When the user then calls ::close on
 * the EpollSys object, it sets a flag in the sys object, wakes up the pthread,
 * which exits immediately after dropping the original reference count returned to
 * the user when the EpollSys was created.  When the reference count goes to 0,
 * the EpollSys object is finally freed.
 */
void *
EpollSys::threadStart(void *argp)
{
    int32_t code;
    int32_t evCount;
    epoll_event ev;
    EpollEvent *ep;
    EpollEvent *nep;
    EpollSys *sysp = (EpollSys *) argp;
    static const uint32_t nevents = 16;
    epoll_event localEvents[nevents];
    epoll_event *eventp;
    uint32_t i;
    
    /* turn this pthread into a dispatcher for a dedicated thread, so we can
     * wait for thread locks correctly.
     */
    ThreadDispatcher::pthreadTop();

    ev.events = EPOLLIN;
    ev.data.ptr = &sysp->_specialEventWakeup;
    code = epoll_ctl(sysp->_epFd, EPOLL_CTL_ADD, sysp->_readWakeupFd, &ev);
    if (code < 0) {
        printf("epoll: add failed for initial pipe\n");
        osp_assert(0);
    }

    while(1) {
        /* collect updates */
        sysp->_lock.take();
        for(ep = sysp->_addingEvents.head(); ep; ep = nep) {
            nep = ep->_dqNextp;
            ev.events = EPOLLONESHOT | ((ep->_flags & EpollEvent::epollIn)? EPOLLIN : EPOLLOUT);
            ev.data.ptr = ep;
            code = epoll_ctl(sysp->_epFd, EPOLL_CTL_ADD, ep->_fd, &ev);
            printf("EPOLL added ep=%p fd=%d code=%d\n", ep, ep->_fd, code);
            if (code < 0) {
                perror("epoll add");
                ep->_failed = 1;
                ep->_triggered = 1;
                ep->_cv.broadcast();
            }
            sysp->_addingEvents.remove(ep);
            sysp->_activeEvents.append(ep);
            ep->_inQueue = EpollEvent::inActiveQueue;
        }

        for(ep = sysp->_removingEvents.head(); ep; ep = nep) {
            nep = ep->_dqNextp;
            /* closing the file descriptor removes it from the epoll set,
             * and the event was put in this queue by close
             */
            sysp->_removingEvents.remove(ep);
            ep->_inQueue = EpollEvent::inRemovingQueue;

            /* this releases the original reference from the initial
             * event creation; we only go through this path after the
             * user calls ::close on the EpollEvent, which resets the
             * event and then triggers the removal of the event from
             * the set of epoll events.
             */
            ep->releaseNL();
        }

        sysp->_running = 0;

        sysp->_lock.release();
        
        /* do the wait */
        evCount = epoll_wait(sysp->_epFd, localEvents, nevents, -1);
        if (evCount < 0) {
            if (errno == EINTR)
                continue;
            osp_assert("epoll_wait failed" == 0);
        }

        sysp->_lock.take();
        sysp->_running = 0;
        for( i=0, eventp = localEvents;
             i<evCount;
             i++, eventp++) {
            ep = (EpollEvent *) eventp->data.ptr;
            if ((uint8_t *) ep != &sysp->_specialEventWakeup) {
                ep->_triggered = 1;
                ep->_cv.broadcast();
            }
            else {
                /* read from the file descriptor */
                uint8_t tc;
                code = read(sysp->_readWakeupFd, &tc, 1);
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

void
EpollSys::wakeThreadNL()
{
    int32_t code;
    uint8_t tc;

    if (!_running) {
        _running = 1;
        tc = 'x';
        code = write(_writeWakeupFd, &tc, 1);
    }
}

void
EpollSys::close()
{
    EpollEvent *ep;
    EpollEvent *nep;
    _lock.take();
    for(ep=_activeEvents.head(); ep; ep=nep) {
        nep = ep->_dqNextp;
        _activeEvents.remove(ep);
        _removingEvents.append(ep);
        ep->_inQueue = EpollEvent::inRemovingQueue;
    }
    for(ep=_addingEvents.head(); ep; ep=nep) {
        nep = ep->_dqNextp;
        _activeEvents.remove(ep);
        _removingEvents.append(ep);
        ep->_inQueue = EpollEvent::inRemovingQueue;
    }
    wakeThreadNL();
    _lock.release();
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
        code = epoll_ctl(_sysp->_epFd, EPOLL_CTL_ADD, _fd, &ev);
        _added = 1;
    }
    else {
        code = epoll_ctl(_sysp->_epFd, EPOLL_CTL_MOD, _fd, &ev);
    }
    if (code < 0) {
        perror("reenableNL");
    }
    _sysp->wakeThreadNL();
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
