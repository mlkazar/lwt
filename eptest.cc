#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "thread.h"
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


class PipeTest : public Thread {
    class PipeReader : public Thread {
        int _fd;
        EpollSys *_sysp;
        EpollEvent *_eventp;
        PipeTest *_ptestp;
    public:
        PipeReader(EpollSys *sysp, PipeTest *ptestp, int fd) {
            _sysp = sysp;
            _ptestp = ptestp;
            _fd = fd;
        }

        void *start();
    };

    class PipeWriter : public Thread {
        int _fd;
        EpollSys *_sysp;
        PipeTest *_ptestp;
        EpollEvent *_eventp;
    public:
        PipeWriter(EpollSys *sysp, PipeTest *ptestp, int fd) {
            _sysp = sysp;
            _ptestp = ptestp;
            _fd = fd;
        }

        void *start();
    };

    uint32_t _iterations;
    EpollSys *_sysp;
    PipeReader *_readerThreadp;
    PipeWriter *_writerThreadp;
    int _pipeFds[2];
public:
    uint32_t getIterations() {
        return _iterations;
    }

    PipeTest(EpollSys *sysp, uint32_t iterations){ 
        pipe(_pipeFds);
        _iterations = iterations;
        _sysp = sysp;

        _readerThreadp = new PipeReader(sysp, this, _pipeFds[0]);
        _readerThreadp->setJoinable();
        _writerThreadp = new PipeWriter(sysp, this, _pipeFds[1]);
    }

    void *start() {
        _readerThreadp->queue();
        _writerThreadp->queue();
        _readerThreadp->join(NULL);
        printf("PipeTest %p done\n", this);
        delete _readerThreadp;
        return NULL;
    }
};

void *
PipeTest::PipeReader::start() {
    int32_t code;
    char tc;

    _eventp = new EpollEvent(_sysp, _fd, /* !isWrite */ 0);
    while(1) {
        _eventp->wait(EpollEvent::epollIn);
        code = read(_fd, &tc, 1);
        if (code != 1) {
            break;
        }
    }
    return NULL;
}

void *
PipeTest::PipeWriter::start() {
    char tc;
    uint32_t maxCount;
    uint32_t count;
    int32_t code;

    _eventp = new EpollEvent(_sysp, _fd, /* isWrite */ 1);
    maxCount = _ptestp->getIterations();
    count  = 0;
    while(count < maxCount) {
        _eventp->wait(EpollEvent::epollOut);
        code = write(_fd, &tc, 1);
        if (code != 1) {
            break;
        }
        count++;
    }
    _eventp->close();
    close(_fd);
    return NULL;
}

int
main(int argc, char **argv)
{
    static const uint32_t maxThreads = 2048;
    uint32_t threadCount;
    uint32_t iterations;
    uint32_t i;
    PipeTest *p;
    EpollSys *sysp;
    PipeTest *testsp[maxThreads];

    if (argc <= 2) {
        printf("usage: eptest <thread count> <count of interations>\n");
        return 0;
    }

    threadCount = atoi(argv[1]);
    iterations = atoi(argv[2]);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);
    
    sysp = new EpollSys("eptest");

    for(i=0;i<threadCount;i++) {
        testsp[i] = p = new PipeTest(sysp, iterations);
        p->setJoinable();
        p->queue();
    }

    for(i=0;i<threadCount;i++) {
        p = testsp[i];
        p->join(NULL);
        printf("PipeTest %d (%p) done\n", i, p);
        delete p;
    }

    _exit(0);

    while(1) {
        sleep(1);
    }
}
