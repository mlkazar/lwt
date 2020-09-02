#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "thread.h"
#include "epoll.h"

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
        int32_t code;
        code = pipe(_pipeFds);
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
    }
};

void *
PipeTest::PipeReader::start() {
    int32_t code;
    char tc;

    _eventp = new EpollEvent(_fd, EpollEvent::epollIn);
    _sysp->addEvent(_eventp);
    while(1) {
        _eventp->wait();
        code = read(_fd, &tc, 1);
        if (code != 1) {
            break;
        }
        _eventp->reenable();
    }
}

void *
PipeTest::PipeWriter::start() {
    char tc;
    uint32_t maxCount;
    uint32_t count;
    int32_t code;

    _eventp = new EpollEvent(_fd, EpollEvent::epollOut);
    _sysp->addEvent(_eventp);
    maxCount = _ptestp->getIterations();
    count  = 0;
    while(count < maxCount) {
        _eventp->wait();
        code = write(_fd, &tc, 1);
        if (code != 1) {
            break;
        }
        count++;
        _eventp->reenable();
    }
    close(_fd);
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
    }

    threadCount = atoi(argv[1]);
    iterations = atoi(argv[2]);

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);
    
    sysp = new EpollSys();

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
