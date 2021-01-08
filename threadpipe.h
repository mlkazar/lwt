#ifndef __CTHREAD_H_ENV__
#define __CTHREAD_H_ENV__ 1

#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "osp.h"
#include "thread.h"
#include "threadmutex.h"

/* a unidirectional pipe */
class ThreadPipe {
 private:
    static const uint32_t _maxBytes = 4096;
    uint32_t _count;
    uint32_t _pos;
    uint8_t _eof;
    char _data[_maxBytes];

    /* lock protecting shared variables.  People wait for CV when
     * buffer is full on write, or buffer is empty on read.  Both
     * don't happen at the same time, so we don't bother having two
     * CVs.
     */
    ThreadMutex _lock;
    ThreadCond _cv;

 public:
    ThreadPipe() : _cv(&_lock) {
        reset();
    }

    void reset() {
        _lock.take();
        _count = 0;
        _pos = 0;
        _eof = 0;
        _lock.release();
    }

    /* write data into the pipe; don't return until all data written */
    int32_t write(const char *bufferp, int32_t count);

    /* read data from the pipe, return any non-zero available data; return
     * 0 bytes if EOF was called on the other side.
     */
    int32_t read(char *bufferp, int32_t count);

    /* called by writer when no more data will be sent */
    void eof();

    /* return true if at EOF */
    int atEof() {
        return _eof;
    }

    void waitForEof();

    int32_t count() {
        int32_t tcount;
        _lock.take();
        tcount = _count;
        _lock.release();
        return tcount;
    }
};
#endif /* __CTHREAD_H_ENV__ */
