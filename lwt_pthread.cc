#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

#include "thread.h"

/*
Binaries linked with the lwt library need to ensure that any pthread created can call
into code implemented with lwt locks, etc.

lwt_thread provides a wrapper implementation of pthread_create.
The wrapper inserts a "intercept" start function, which first initializes the pthread
to make it compatible with lwt (pthreadTop), and then calls the "real" pthread
start function.
 */

typedef int(pthread_create_t)(pthread_t *, const pthread_attr_t *, void *(void *), void *);

struct intercept_arg
{
	void *(*fn)(void *);
	void *arg;
};

#define LWT_PTHREAD_INTERCEPT 1
#ifdef LWT_PTHREAD_INTERCEPT
extern "C"
{

	void *pthread_intercept_start(void *arg)
	{
		intercept_arg *pIntercept = (intercept_arg *)(arg);
		void *(*real_start)(void *);
		void *real_arg = pIntercept->arg;
		real_start = pIntercept->fn;
		free(pIntercept);

		ThreadDispatcher::pthreadTop();

		return real_start(real_arg);
	};

	int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
	{
		static pthread_create_t *pthread_create_real = (pthread_create_t *)(dlsym((void *)-1l, "pthread_create"));

		intercept_arg *pIntercept = (intercept_arg *)(malloc(sizeof(struct intercept_arg)));
		pIntercept->fn = start;
		pIntercept->arg = arg;

		return pthread_create_real(thread, attr, pthread_intercept_start, pIntercept);
	}
};
#endif /* LWT_PTHREAD_INTERCEPT */