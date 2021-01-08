#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osp.h"
#include "threadpipe.h"

/* our reader just reads 20K, and then indicates that it is finished,
 * and waits for the next iteration to start.
 * 
 * the writer writes 20K, waits for the reader to finish, and then
 * resets the pipe, and starts writing again, after signalling that
 * the next iteration has started (nextIterationReady is set).
 */
class PipeTest : public Thread {
public:
    class PipeReader;
    class PipeWriter;

    ThreadPipe *_pipep;
    ThreadMutex _lock;
    ThreadCond _cv;
    static const int32_t _bytesPerTrip = 20000;
    uint32_t _spins;
    uint8_t _readerDone;
    uint8_t _nextIterationReady;
    PipeReader *_readerp;
    PipeWriter *_writerp;

public:
    class PipeReader : public Thread {
        PipeTest *_pipeTestp;

    public:
        void *start() {
            uint32_t i;
            char tbuffer[1024];
            ThreadPipe *pipep = _pipeTestp->_pipep;
            int32_t bytesLeft;
            int32_t code;

            /* read bytesPerTrip from the pipe and then wakeup the sender */
            for(i=0;i<_pipeTestp->_spins;i++) {
                printf("*reader %p new iteration %d\n", this, i);
                
                _pipeTestp->_lock.take();
                _pipeTestp->_nextIterationReady = 0;
                _pipeTestp->_lock.release();

                bytesLeft = _bytesPerTrip;
                while(bytesLeft > 0) {
                    code = pipep->read(tbuffer, sizeof(tbuffer));
                    if (code < (signed) sizeof(tbuffer)) {
                        break;
                    }
                }

                _pipeTestp->_lock.take();
                _pipeTestp->_readerDone = 1;
                _pipeTestp->_cv.broadcast();

                /* now wait until the writer has prepared the next iteration.  The
                 * writer will prepare it by reseting the pipe.
                 */
                while(!_pipeTestp->_nextIterationReady) {
                    _pipeTestp->_cv.wait();
                }
                _pipeTestp->_lock.release();
            }

            return NULL;
        }

        PipeReader(PipeTest *pipeTestp) {
            _pipeTestp = pipeTestp;
        }
    };

    class PipeWriter : public Thread {
    public:
        PipeTest *_pipeTestp;

        PipeWriter(PipeTest *pipeTestp) {
            _pipeTestp = pipeTestp;
        }

        void *start() {
            ThreadPipe *pipep = _pipeTestp->_pipep;
            uint32_t i;
            int32_t bytesLeft;
            char tbuffer[1000];
            int32_t bytesThisWrite;
            int32_t code;

            memset(tbuffer, 'a', sizeof(tbuffer));
            for(i=0;i<_pipeTestp->_spins;i++) {
                printf("*writer %p new iteration %d\n", this, i);
                _pipeTestp->_lock.take();
                _pipeTestp->_readerDone = 0;
                _pipeTestp->_lock.release();

                bytesLeft = _bytesPerTrip;
                while(bytesLeft > 0) {
                    bytesThisWrite = (bytesLeft > (signed) sizeof(tbuffer)?
                                      sizeof(tbuffer) : bytesLeft);
                    code = pipep->write(tbuffer, bytesThisWrite);
                    bytesLeft -= code;
                }
                /* and mark that we're done */
                pipep->eof();

                _pipeTestp->_lock.take();
                while(!_pipeTestp->_readerDone) {
                    _pipeTestp->_cv.wait();
                }

                /* reset the pipe, and then start the reader up again by setting 
                 * nextIterationReady.
                 */
                pipep->reset();
                _pipeTestp->_nextIterationReady = 1;

                _pipeTestp->_cv.broadcast();

                _pipeTestp->_lock.release();
            }

            return NULL;
        }
    };

    PipeTest(uint32_t spins) {
        _pipep = new ThreadPipe();
        _readerDone = 0;
        _nextIterationReady = 0;
        _cv.setMutex(&_lock);
        _spins = spins;
        _readerp = NULL;
        _writerp = NULL;
    };

    void *start() {
        void *junkp;

        _readerp = new PipeReader(this);
        _readerp->setJoinable();
        _writerp = new PipeWriter(this);
        _writerp->setJoinable();

        _readerp->queue();
        _writerp->queue();

        /* now wait for the tests to finish */
        _readerp->join(&junkp);
        _writerp->join(&junkp);

        printf("Subtest done\n");
        return NULL;
    };
};

int
main(int argc, char **argv)
{
    static const uint32_t _maxThreads = 100;
    PipeTest *pipeTestp[_maxThreads];
    void *junkp;
    uint32_t spins = 100;
    uint32_t nthreads = 2;
    uint32_t i;
    
    if (argc >= 2) {
        if (argv[1][0] == '-') {
            printf("usage: pipetest <nthreads=2> <spins=100>\n");
            return -1;
        }
    }

    if (argc >= 3) {
        nthreads = atoi(argv[1]);
        spins = atoi(argv[2]);
    }

    if (nthreads > _maxThreads)
        nthreads = _maxThreads;

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);

    for(i=0;i<nthreads;i++) {
        pipeTestp[i] = new PipeTest(spins);
        pipeTestp[i]->setJoinable();
        printf("PipeTest thread=%p\n", pipeTestp[i]);
        pipeTestp[i]->queue();
    }

    for(i=0;i<nthreads;i++) {
        pipeTestp[i]->join(&junkp);
    }

    printf("Done with pipetest\n");
}
