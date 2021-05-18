# Thread Package

This document describes the Thread package and its associated
classes. It implements a light-weight threading model where threads are
cooperatively multitasked on individual processors, but different
threads may run concurrently on different processors.

The goal is to provide threads without any corresponding kernel
structures, and with very low thread switching latencies.  Because
there are no corresponding kernel structures, we expect to be able to
create 100K-1M threads, if necessary, in a single Unix process.  In
addition, thread switch times can be very fast, typically
sub-microsecond from the time one thread wakes up another, and the
time the second thread begins execution.

## API Overview

The thread package is initialized by calling the static function
```
    ThreadDispatcher::setup(uint16_t ndispatchers);
```
with the desired number of dispatcher pthreads.  In addition to
creating _ndispatcher_ pthreads, it also converts the main thread into
a Thread which can call Thread library functions.

After the thread library is initialized, other pthreads can be
converted into Threads by calling the static function
`ThreadDispatcher::pthreadTop()`.  These pthreads aren't running
regular dispatchers, so will be idle unless their one thread is
executing.

New threads can be created by calling new Thread.  There are two
signatures:

```
	threadp = new Thread("name", stackSize);

	threadp = new Thread(stackSize);
```

The stackSize parameter is optional and has a default size (128K).

Once a thread has been created, it can begin execution at its `start`
method by calling queue on the thread

```
threadp->queue()
```

The start method has the following signature: `void *start()`.  If the
`start` method returns, the thread will be destroyed (by the C++
`delete` operator), and any returned value will be available to a join
operation.

Normally, when a thread exits by returning from its `start` method, it
is immediately deleted, but if the creator calls the `setJoinable()`
method on the thread, the thread will wait for someone to call the
thread's `join` method before deleting the thread.  The `join` call
will return the value returned from the `start` method.

Note that you can also call `exit(void *p)` from a thread to terminate the thread
without bothering to return from the start method.

## Implementation Creating a new thread also creates a context (see
makecontext/getcontext/setcontext C library functions) that begins
execution at ctxStart on the new stack.  Once a dispatcher calls
setcontext on that context, the thread will execute a bit of code that
calls the thread's start method and then calls exit if start returns.

When a thread needs to sleep, it calls `Thread:sleep(SpinLock
*lock)`.  This will atomically put the thread to sleep and release the
spin lock, such that no other thread can wake up the thread calling
sleep until the spin lock has been released.  Typically, threads
don't call sleep directly but instead use condition variables or
mutexes, which call sleep internally.

The `sleep` method works by saving the stack context in the thread's
_ctx state, and then switching stacks to the idle task, which will
search for a new runnable thread.  Note that if we tried to find a new
thread to run while still on the sleeping thread's stack, then if the
sleeping thread wakes while we're starting up a new thread, we'll
be sharing the stack between the newly woken thread and the
dispatcher.

Note also that the getcontext function doesn't tell its caller whether
it returned after saving the context, or whether the context has just
been restored and the thread is waking up again.  In the first case,
we want to switch to the idle thread to find a new thread to run,
while in the latter case, we want to return from `sleep`.  Instead, we
use a bit of state in the Thread structure to tell us if we're still
saving the context or not.  Since the thread can't wake up until the
first setcontext call switches to the idle loop and drops our spin
lock, we're guaranteed that no one else will messs with the flag in
the Thread structure while we're using it.

## Locking Rules

## Usage Examples

## Warnings

Watch for stack overflows.  At present we don't have a guard page at
the bottom of each stack, so a stack overflow will simply cause memory
corruption.
