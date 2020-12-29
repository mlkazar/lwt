#ifndef __SPINLOCK_H_ENV__
#define __SPINLOCK_H_ENV__ 1

/* a simple spin lock, available to external callers */
class SpinLock {
 public:
    std::atomic<int> _owningPid;

    SpinLock() {
        _owningPid = 0;
    }

    /* grab the lock */
    void take() {
        int exchangeValue;
        int newValue;

        while(1) {
            exchangeValue = 0;
            newValue = 1;
            if (_owningPid.compare_exchange_strong(exchangeValue, 
                                                   newValue,
                                                   std::memory_order_acquire)) {
                /* success */
                break;
            }
            else {
                continue;
            }
        }
    }

    /* return true if we get the lock, but never block */
    int tryLock() {
        int exchangeValue;
        exchangeValue = 0;
        if (_owningPid.compare_exchange_weak(exchangeValue, 1, std::memory_order_acquire)) {
            /* success */
            return 1;
        }
        else {
            return 0;
        }
    }

    /* release the lock */
    void release() {
        _owningPid.store(0, std::memory_order_release);
    }
};

class Once {
    typedef void (OnceProc) (void *handlep);
    SpinLock _lock;
    uint8_t _called;
 public:
    Once() {
        _called = 0;
    }

    int call(OnceProc *procp, void *contextp);
};

#endif /* SPINLOCK */
