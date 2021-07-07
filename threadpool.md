# ThreadPool Package

This document describes the ThreadPool package, which provides an abstraction of a pool of already created threads which can be allocated and freed at the cost of a ThreadCond wakeup, instead of a large memory allocation.  In practice, this might be 0.2 microseconds compared with 70 microseconds.

The ThreadPool class has two virtual subclasses: a Worker class that defines the methods used for defining a thread participating in the ThreadPool, and a WorkerFactory class that is used to allocate new Workers when the current set of workers are all busy and the pool has not yet reached its limit of allocated workers.  Both of these classes must be subclassed by the user of the ThreadPool.

To create a ThreadPool, you simply allocate a ThreadPool object and initialize it with the maximum number of threads it can create, and a pointer to a ThreadPool::WorkerFactory.  The WorkerFactory has one virtual method, newWorker, which simply returns a newly allocated Worker.

To use the ThreadPool, you subclass ThreadPool::Worker to be used in whatever means you want, and then write a short factory class to generate those workers, and pass that factory to the ThreadPool's initialization function.

Once the ThreadPool is created, the pool's owner can allocate a worker from the pool by calling `ThreadPool::get`; this call can be executed synchronously or asynchronously; when executed asynchronously (with its wait parameter set to false) if the maximum number of threads are in use, the call fails.  The worker's owner can call any custom methods on the Worker's subclass to configure the new thread, and then calls `ThreadPool::Worker::tpResume` to start the worker at its `::tpStart` method.

When the worker terminates, the worker is held waiting for the owner to call either `::join` on the worker, or `::joinAny` on the thread pool.  Once the worker has been joined, it still remains pending, available for the caller to invoke any available methods on the worker, until the joiner calls `ThreadPool::Worker::tpFinished` on the worker, at which time the Worker is freed to await a new `ThreadPool::get` request.

If the pool's owner doesn't need to be notified when a particular Worker completes, the owner can call `::tpIdleOnExit` on the worker before calling `::tpResume` on it, and the thread will immediately be available for reuse upon the return of `::tpStart`.

## ThreadPool API

`ThreadPool::WorkerFactory` is a virtual class that gets subclassed to provide a newWorker.

`ThreadPool::Worker *ThreadPool::WorkerFactory::newWorker()`
This function is responsible for allocating a new subclassed Worker object.

`ThreadPool::init( uint32_t nworkers, ThreadPool::WorkerFactory *factoryp)`
This function initializes a thread pool.

`ThreadPool::Worker::tpStart()` Must be provided by the Worker's subclass.  The `::tpStart` function runs each time the Worker is resumed via `::tpResume` and the worker terminates when the `::tpStart` function returns.

`ThreadPool::Worker::tpJoin()` This call waits until a worker terminates (by returning from its tpStart function), returning the value returned by the tpStart function.

`ThreadPool::Worker::tpIdleOnExit()` This call sets the worker to terminate immediately upon returning from tpStart, instead of waiting at that point for a tpJoin call.

`int ThreadPool::Worker::getIdleOnExit()` returns true if the idleOnExit flag is set.

`ThreadPool::Worker::tpResume()` This is called to start a newly allocated worker after it has been allocated by `ThreadPool::get`.

`ThreadPool::Worker::tpFinished()`  This is called after a thread has been returned by tpJoin (or tpJoinAny), to free the thread.

`ThreadPool::get(Worker **workerp, int wait=1)` Return an idle worker thread from the thread pool.  If wait is 0, and all threads in the pool are busy, the non-zero error code TP_ERR_ALL_RUNNING is returned, and no worker is returned.

`ThreadPool::joinAny(Worker **workerp, void **valuep, int wait=1)` Return any joinable (terminated) Worker, and its returned value in *valuep.  If wait is false and no joinable threads are available, a non-zero code, TP_ERR_ALL_DONE is returned.

## Internal Locking

The package uses a single per-thread pool mutex to synchronize itself.

## Usage Examples

Here's a trivial example of an application that uses a thread pool to compute an expensive function of a set of integers, and adds them all together as they complete.
```
class DemoWorker : public ThreadPool::Worker {
		uint32_t _inputValue;
		uint32_t _outputValue;

	public:
		void setInput(uint32_t inputValue)  {
			_inputValue = inputValue;
		}

		void getResult() {
			return _outputValue;
		}

		/* obviously *not* an expensive function, but a placeholder
		 * for one.
		 */
		uint32_t expensiveFunction(uint32_t inputValue) {
			return inputValue * inputValue;
		}

		/* this function is executed on every worker activation */
		void *tpStart() {
			_outputValue = expensiveFunction(_inputValue);
		}
};

class DemoWorkerFactory : public ThreadPool::WorkerFactory {
	public:

	Worker *newWorker() {
		return (Worker *) new DemoWorker();
	}
};

class Collector : public Thread {
	ThreadPool *_poolp;
	DemoWorker *workerp;
	uint32_t _count;
	public:

	void init(ThreadPool *poolp, uint32_t count) {
		_poolp = poolp;
		_count = count;
		queue();	/* start */
	}
	void *start() {
		uint32_t i;
		uint32_t *valuep;
		uint32_t sum=0;

		for(i=0; i<_count; i++) {
			code = _poolp->joinAny(&workerp, NULL);
			if (code == 0) {
				sum += workerp->getResult();
				workerp->tpFinish();
			}
		}
		valuep = new uint32_t;
		*valuep = sum;
		return valuep;
	}
}

int32_t
main(int argc, char **argv)
{
	DemoWorkerFactory factory;
	ThreadPool *basicPoolp;
	uint32_t i;
	Collector collector;
	DemoWorker *workerp;
	uint32_t count = 1000;
	uint32_t *valuep;

	/* initializing threading system in main */
	ThreadDispatcher::setup(4);	/* use up to 4 pthreads */
	
	basicPoolp = new ThreadPool();
	basicPoolp->init(32, &factory);
	
	/* run a separate collector thread to receive count results */
	collector.setJoinable();
	collector.init(basicPoolp, count);
	
	/* next, compute the expensive function on the first 1000
	 * integers, using the threadpool
	 */
	for(i=0;i<count;i++) {
		/* ignore return code, since synchronous get doesn't
		 * return error codes.  Note that get will block if
		 * all the workers are busy and not yet joined.
		 */
		basicPoolp->get(static_cast<ThreadPool::Worker **>(&workerp));

		workerp->setInput(i);
		workerp->resume();
	}
	
	/* wait for the collector to get all the results */
	collector.join(&valuep);
	printf("Sum is %d\n", *valuep);
	delete valuep;
}
```
In this example, we run 32 threads concurrently computing the "expensive" function, with a collector thread that receives the results and adds them together.  When the collector has received all the results, it terminates, returning the result.

Note that we use a separate collector because if we don't collect worker completions as they are occur, the main thread will get stuck calling `ThreadPool::get` when all the threads are waiting to be tpJoined.

## Warnings

You must be careful not to allocate an unbounded number of worker threads without having an asynchronous task doing the corresponding tpJoin operations to allow the worker's to terminate and reenter the thread pool.

## Improvements

Perhaps we should have a single call that waits for either a thread completion or a new idle thread to become available, so we don't have to spin up a collector thread when we're firing up a large number of workers.  Essentially, this would combine the functionality of `ThreadPool::tpJoinAny` and `ThreadPool::get` into a function that completes when either function would complete.

