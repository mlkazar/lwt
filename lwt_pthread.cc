#include <iostream>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <pthread.h>

//#include <boost/stacktrace.hpp>

#include "thread.h"
#include "Exception.h"

using namespace std;

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

static void dumpExceptionHeader(
    ostream &ost,
    const char *what,
    const std::string *detailsp,
    boost::stacktrace::stacktrace *stacktracep)
{
    auto currthread = Thread::getCurrent();
    pid_t pthreadId = gettid();
    char pthreadName[64];
    memset(pthreadName, 0, 64);
    pthread_getname_np(pthread_self(), pthreadName, 64);

    ost << "Unhandled Exception: " << what << endl;
    ost << "lwt thread " << currthread
        << " (" << currthread->name() << ")" << endl;
    ost << "pthread " << pthreadId
        << " (" << pthreadName << ")" << endl;

    if (detailsp && detailsp->size())
    {
        cerr << "Details:" << endl
             << *detailsp << endl;
    }
    if (stacktracep && stacktracep->size())
    {
        cerr << "Stacktrace:" << endl
             << *stacktracep << endl;
    }
}

static void *pthread_intercept_start(void *arg)
{
    intercept_arg *pIntercept = (intercept_arg *)(arg);
    void *(*real_start)(void *);
    void *real_arg = pIntercept->arg;
    real_start = pIntercept->fn;
    free(pIntercept);

    ThreadDispatcher::pthreadTop();
    try
    {
        return real_start(real_arg);
    }
    catch (Exception &e)
    {
        dumpExceptionHeader(cerr, e.what(), e.Details(), e.Stacktrace());
        assert("unhandled Exception" == nullptr);
    }
    catch (std::exception &e)
    {
        const auto detail = boost::get_error_info<ExceptionDetail>(e);
        auto stacktrace = boost::get_error_info<ExceptionStackTrace>(e);
        dumpExceptionHeader(cerr, e.what(), detail, stacktrace);
        assert("unhandled std::exception" == nullptr);
    }
};

#define LWT_PTHREAD_INTERCEPT 1
#ifdef LWT_PTHREAD_INTERCEPT
extern "C"
{

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