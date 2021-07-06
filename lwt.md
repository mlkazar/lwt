# Thread Package

This document describes the Thread package and its associated
classes. It implements a light-weight threading model where threads are cooperatively multitasked on individual processors, but different threads may run concurrently on different processors.

The goal is to provide threads without any corresponding kernel
structures, and with very low thread switching latencies.  Because
there are no corresponding kernel structures, we expect to be able to create 100K-1M threads, if necessary, in a single Unix process.  In addition, thread switch times can be very fast, typically
sub-microsecond from the time one thread wakes up another, and the time the second thread begins execution.

## Thread API
This section describes the interface to the thread package, and its related modules.

### Thread basics

The thread package is initialized by calling the static function
```
    ThreadDispatcher::setup(uint16_t ndispatchers);
```
with the desired number of dispatcher pthreads.  In addition to
creating _ndispatcher_ pthreads, it also converts the main thread into a Thread which can call Thread library functions.

After the thread library is initialized, other pthreads can be
converted into Threads by calling the static function
`ThreadDispatcher::pthreadTop()`.  These pthreads aren't running regular dispatchers, so will be idle unless their one thread is executing.

New threads can be created by calling new Thread.  There are two
signatures:

```
	threadp = new Thread("name", stackSize);

	threadp = new MyThread(stackSize);
```

where MyThread is a subclass of Thread.  The stackSize parameter is optional and has a default size (128K).

Once a thread has been created, it can begin execution at its `start` method by calling `queue` on the thread

```
threadp->queue()
```

The start method has the following signature: `void *start()`.  If the`start` method returns, the thread will be destroyed (by the C++ `delete` operator), and any returned value will be available to a join operation.

Normally, when a thread exits by returning from its `start` method, it is immediately deleted, but if the creator calls the `setJoinable()`method on the thread, the thread will wait for someone to call thethread's `join` method before deleting the thread.  The `join` call will return the value returned from the `start` method.

Note that you can also call `exit(void *p)` from a thread to terminate the thread without bothering to return from the start method.

### Joining threads

Normally, when a thread terminates by exiting or returning, the thread's resources are immediately freed.  However, if you call `Thread::setJoinable` on the thread, the thread will wait when it  exits until another thread calls the `Thead::join` method.  The `::join` method waits until the thread exits, and then returns the value returned by the `start` method, or the value passed to the `exit` method.

### Low-level synchronization

The thread package contains a sleep operation useful for implementing condition variable style functionality.  To implement a condition variable, recall that you must atomically drop a mutex or other type of lock and sleep, atomically.

If the condition variable is protected by a SpinLock, the implementor of such a package can call `Thread::sleep(&lock)`, where `lock` is a SpinLock.  The Thread package will atomically drop the lock and put the thread to sleep, so that any thread executing after `lock` is release will see the thread sleeping, so that `::queue` is safe to apply to the sleeping thread and will wake the sleeping thread.

### Miscellaneous operations

The static method `Thread::getCurrent()` returns the currently executing thread.

The method `Thread::setName`changes the thread name.


### Implementation

Creating a new thread also creates a context (see makecontext/getcontext/setcontext C library functions) that begins execution at ctxStart on the new stack.  Once a dispatcher calls setcontext on that context, the thread will execute a bit of code that calls the thread's start method and then calls exit if start returns.

When a thread needs to sleep, it calls `Thread:sleep(SpinLock
*lock)`.  This will atomically put the thread to sleep and release the spin lock, such that no other thread can wake up the thread calling sleep until the spin lock has been released.  Typically, threads don't call sleep directly but instead use condition variables or mutexes, which call sleep internally.

The `sleep` method works by saving the stack context in the thread's _ctx state, and then switching stacks to the idle task, which will search for a new runnable thread.  Note that if we tried to find a new thread to run while still on the sleeping thread's stack, then if the sleeping thread wakes while we're starting up a new thread, we'll be sharing the stack between the newly woken thread and the dispatcher.

Note also that the getcontext function doesn't tell its caller whether it returned after saving the context, or whether the context has just been restored and the thread is waking up again.  In the first case, we want to switch to the idle thread to find a new thread to run, while in the latter case, we want to return from `sleep`.  Instead, we use a bit of state in the Thread structure to tell us if we're still saving the context or not.  Since the thread can't wake up until the first setcontext call switches to the idle loop and drops our spin lock, we're guaranteed that no one else will messs with the flag in the Thread structure while we're using it.

## ThreadMutex API
The ThreadMutex class provides a simple mutual exclusion lock.  The ThreadMutex::take method obtains the lock, blocking the thread if necessary. The ThreadMutex::release method releases the mutex, waking up one other thread.  The ThreadMutex::tryLock method never blocks, and returns 1 if the lock is successfully obtained, and 0 if the lock is held by someone else.

### Implementation
The mutex internals are protected by a spin lock, and the mutex implementation uses Thread::sleep to release the lock and sleep atomically.

ThreadMutex is a subclass of ThreadBaseLock, which provides 5 operations matching methods in ThreadMutex:

The ::take, ::tryLock and ::release methods are described above.

The ::getWaitUs method returns the number of microseconds of time that threads have been waiting for the lock.

The ::releaseAndSleep(Thread *threadp) atomically releases the ThreadBaseLock and puts threadp to sleep.

## ThreadCond API

The ThreadCond API works in tandem with an instance of a ThreadBaseLock, such as a ThreadMutex or ThreadLockRw, and implements a condition variable.

ThreadCond::setMutex(ThreadBaseLock *lockp) sets the default lock associated with the condition variable.

ThreadCond::wait(ThreadBaseLock *lockp=0) releases the lock and waits for the condition variable to be signaled or broadcast.  If a non-null lockp is provided, it is used as the lock to release.  If lockp is null, the default lock is used, and if neither is set, the call fails.

ThreadCond::signal wakes exactly one waiting thread.

ThreadCond::broadcast wakes all waiting threads.

## ThreadLockRw

This implements a read write lock, and is also a subclass of ThreadBaseLock. 

All of the lock operations take an optional tracker object.  If trackerp is non-null, the tracker object is put in a list hanging off the lock, and is removed when the lock is released, and can be used from gdb or an application to find all of the read, write and upgrade lock owners.  If the tracker object is provided in the lock call, it must be provided in a corresponding release call.  And of course, the variable must not fall out of scope while it's active.

The ::lockRead(ThreadLockTracker *trackerp) obtains a read lock.

The ::lockWrite(ThreadLockTracker *trackerp) obtains a write lock.

The ::lockUpgrade(ThreadLockTracker *trackerp) obtains an upgradeable read lock.  At most one such lock can be obtained at any one time, but an upgradeable lock is compatible with any number of readers, and can be upgraded to a write lock without letting any other writers grab the lock during the upgrade.

The ::releaseWrite(ThreadLockTracker *trackerp) method releases a write lock.

The ::releaseRead(ThreadLockTracker *trackerp) method releases a read lock.

The ::releaseUpgrade(ThreadLockTracker *trackerp) method releases an un-upgraded upgrade lock.  If the lock has been upgraded to a write lock, you must use ::releaseWrite to release it.

The ::upgradeToWrite() method upgrades from an upgrade lock to a write lock, while not allowing any writers to intervene, so that no invariants protected by the upgrade lock can be invalidated by another thread.

The ::writeToRead method converts a write lock to a read lock, again without allowing any other writers to obtain the lock in the interim.

The base lock operations map to write lock operations.

Note that because a ThreadLockRw is a subclass of ThreadLockBase, you can use this type of lock with ThreadCond condition variables.  However, you can only release a write lock with ThreadCond::wait.

## ThreadTimer
The ThreadTimer module implements a simple timer mechanism whereby a static function will be called by a timer manager thread after a certain number of milliseconds

Before the timer package can be used, the main program must call the static method `ThreadTimer::init()` although pthread_once can be used to invoke this as well.

To use a timer, you call
```
new ThreadTimer(ms, &callbackProc, callbackContextp)
```
where ms is the number of milliseconds for the timer, callback has the signature `void callback(ThreadTimer *timerp, void *contextp)`.  A timer can be canceled by calling its `::cancel` method, which will also release the reference to the timer.  The cancel function returns 1 if the timer was canceled, and 0 if it is too late to prevent the timer from firing.  The method `::isCanceled` returns 1 if the timer has been canceled, and 0 otherwise.

Note that there's an inherent race between canceling a timer and the timer's firing.  If a timer is canceled at the same time that the timer package has called the callback function, but the callback function hasn't begun execution, there's essentially nothing the timer package can do to prevent the callback function from executing.  If the code that cancels the timer also frees the context object, the timer callback will crash.

There are several ways of dealing with this race condition.

Option 1: static lock protecting timer:

```
void deleter(Context *cxp) {
	_lock.take();
	cxp->_timerp->cancel();
	delete cxp;
	_lock.release();
}

void
TimerCallback(ThreadTimer *timerp, void *contextp) {
	_lock.take();
	if (timerp->isCanceled()) {
		_lock.release();
		/* in this branch, it is critical not to touch *contextp
		 * since it may be freed
		 */
		return;
	}
	Context *objp = (Context *) contextp;
	...
	do_random_stuff_with(objp);
	_lock.release();
}
```
In this pattern, if the context object is deleted, and we're holding the lock, then the timer will be canceled as well.  Note that it's important that we don't put _lock in the context object, or we won't be able to safely take the lock.

<<<if cancel returns failure, set deleted flag adn have timer deleet the object>>>

<<<ref count

## Locking Rules
Nothing too complex here.

## Usage Examples

## Warnings

Watch for stack overflows.  At present we don't have a guard page at the bottom of each stack, so a stack overflow will simply cause memory corruption.
