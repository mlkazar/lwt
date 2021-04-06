#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "threadmutex.h"
#include "threadtimer.h"

class BasicTestState {
public:
    uint32_t _totalSpins;
    uint32_t _currentSpin;
    uint32_t _maxSpins;

    BasicTestState() {
        _totalSpins = 0;
        _currentSpin = 0;
        _maxSpins = 0;
    }

    void setTotalSpins(uint32_t totalSpins) {
        _totalSpins = totalSpins;
    }
};

class MonitorTest : public Thread {
public:
    BasicTestState *_statep;
    
    void *start() {
        while(1) {
            printf("Currently on spin %d of %d\n", _statep->_currentSpin, _statep->_totalSpins);
            ThreadTimer::sleep(4000);
        }
        return NULL;
    };

    MonitorTest(BasicTestState *statep) {
        _statep = statep;
    }
};

class MutexTest : public Thread {
public:
    class TestState : public BasicTestState {
    public:
        ThreadMutex _lock;
        uint32_t _exclCounter;

        TestState() {
            _exclCounter = 0;
        }
    };

    TestState *_statep;

    void *start() {
        uint32_t value;
        uint32_t spin;

        printf("Starting thread %p\n", Thread::getCurrent());
        for(spin = 0; spin < _statep->_maxSpins; spin++) {
            if (random() % 1) {
                /* we're just going to check the counter for consistency */
                _statep->_lock.take();
                value = _statep->_exclCounter;
                ThreadTimer::sleep(1);
                assert(value == _statep->_exclCounter);
                ThreadTimer::sleep(1);
                assert(value == _statep->_exclCounter);
                _statep->_lock.release();
            }
            else {
                /* we're going to change and check the counter */
                _statep->_lock.take();
                ++_statep->_exclCounter;
                value = _statep->_exclCounter;
                ThreadTimer::sleep(1);
                assert(value == _statep->_exclCounter);
                ThreadTimer::sleep(1);
                assert(value == _statep->_exclCounter);
                _statep->_lock.release();
            }
            
            _statep->_currentSpin++;
        }
        return NULL;
    }

    MutexTest(TestState *statep, uint32_t spins) {
        _statep = statep;
        statep->_maxSpins = spins;
    }
};

class RwTest : public Thread {
public:
    /* exclCounter is only examined with at least a read lock, and is
     * updated only with a write lock.  sharedCounter is updated and
     * examined with a read lock, and we expect to see it change while
     * holding a read lock, if we really have concurrency between
     * multiple readers.  The sharedRaces counter goes up any time we
     * see sharedCounter change while holding a read lock, and
     * upgradeRaces goes up any time we see sharedCounter change while
     * holding an upgrade lock.  We expect both to be non-zero.
     */
    class TestState : public BasicTestState {
    public:
        ThreadLockRw _lock;
        uint32_t _exclCounter;
        uint32_t _sharedCounter;
        uint32_t _sharedRaces;
        uint32_t _upgradeRaces;

        TestState() {
            _exclCounter = 0;
            _sharedCounter = 0;
            _sharedRaces = 0;
            _upgradeRaces = 0;
        }
    };

    TestState *_statep;

    void *start() {
        uint32_t value;
        uint32_t sharedValue;
        uint32_t spin;
        uint32_t testIndex;
        ThreadLockTracker trackState;

        printf("Starting thread %p\n", Thread::getCurrent());
        for(spin = 0; spin < _statep->_maxSpins; spin++) {
            testIndex = random() % 4;
            switch (testIndex) {
                case 0:
                    /* we're just going to check the counter for consistency */
                    _statep->_lock.lockRead(&trackState);
                    value = _statep->_exclCounter;
                    ThreadTimer::sleep(1);
                    sharedValue = ++(_statep->_sharedCounter);
                    assert(value == _statep->_exclCounter);
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);
                    if (sharedValue != _statep->_sharedCounter)
                        _statep->_sharedRaces++;
                    _statep->_lock.releaseRead(&trackState);
                    break;

                case 1:
                    /* we're going to change and check the counter */
                    _statep->_lock.lockWrite(&trackState);
                    ++_statep->_exclCounter;
                    value = _statep->_exclCounter;
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);
                    _statep->_lock.releaseWrite(&trackState);
                    break;

                case 2:
                    /* we're going to change and check the counter */
                    _statep->_lock.lockUpgrade(&trackState);
                    value = _statep->_exclCounter;
                    sharedValue = ++(_statep->_sharedCounter);
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);
                    if (sharedValue != _statep->_sharedCounter)
                        _statep->_upgradeRaces++;
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);

                    /* now upgrade to write; upgrading should not let any writers in */
                    _statep->_lock.upgradeToWrite();
                    ThreadTimer::sleep(2);
                    /* this assert is important, as it validates that
                     * upgrades don't let other updates in.
                     */
                    assert(value == _statep->_exclCounter);
                    value = ++_statep->_exclCounter;
                    ThreadTimer::sleep(2);
                    assert(value == _statep->_exclCounter);
                    _statep->_lock.releaseWrite(&trackState);
                    break;

                case 3:
                    /* we're going to check the counter */
                    _statep->_lock.lockUpgrade(&trackState);
                    value = _statep->_exclCounter;
                    sharedValue = ++(_statep->_sharedCounter);
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);
                    ThreadTimer::sleep(1);
                    assert(value == _statep->_exclCounter);
                    if (sharedValue != _statep->_sharedCounter)
                        _statep->_upgradeRaces++;

                    /* now upgrade to write; upgrading should let any writers in */
                    _statep->_lock.releaseUpgrade(&trackState);
                    break;

                default:
                    ThreadTimer::sleep(4);
            } /* switch statement */
            
            _statep->_currentSpin++;
        } /* spin loop */
        return NULL;
    }

    RwTest(TestState *statep, uint32_t spins) {
        _statep = statep;
        statep->_maxSpins = spins;
    }
};

int
main(int argc, char **argv)
{
    uint32_t i;
    MutexTest::TestState mutexState;
    RwTest::TestState rwState;
    BasicTestState *basicTestStatep;
    uint32_t spins;
    uint32_t threads;
    static const uint32_t maxThreads = 512;
    Thread *childThreadsp[maxThreads];
    MonitorTest *monitorp;
    int rwTest = 0;

    if (argc < 2) {
        printf("usage: locktest {mutex,rwlock} <threads=8> <spins=1000>\n");
        return -1;
    }

    /* start the dispatcher */
    ThreadDispatcher::setup(/* # of pthreads */ 2);
    ThreadTimer::init();

    threads = 8;
    spins = 1000;

    if (argc > 2)
        threads = atoi(argv[2]);

    if (argc > 3)
        spins = atoi(argv[3]);

    if (threads > maxThreads)
        threads = maxThreads;

    printf("Running %d test threads with %d spins\n", threads, spins);

    if (strcmp(argv[1], "mutex") == 0) {
        /* create all the threads */
        basicTestStatep = &mutexState;
        for(i=0;i<threads;i++) {
            childThreadsp[i] = new MutexTest(&mutexState, spins);
        }
    }
    else if (strcmp(argv[1], "rwlock") == 0) {
        rwTest = 1;
        basicTestStatep = &rwState;
        for(i=0;i<threads;i++) {
            childThreadsp[i] = new RwTest(&rwState, spins);
        }
    }
    else {
        printf("unknown test '%s'\n", argv[1]);
        return -1;
    }

    basicTestStatep->setTotalSpins(spins * threads);

    for(i=0; i<threads; i++) {
        childThreadsp[i]->setJoinable();
        childThreadsp[i]->queue();
    }

    monitorp = new MonitorTest(basicTestStatep);
    monitorp->queue();
    

    for(i=0; i<threads; i++) {
        childThreadsp[i]->join(NULL);
    }

    if (rwTest) {
        printf("SharedRaces=%d UpgradeRaces=%d\n", rwState._sharedRaces, rwState._upgradeRaces);
        printf("RwLock state counter=%ld\n", rwState._lock._trackerQueue.count());
    }

    printf("All tests done\n");

    return 0;
}
