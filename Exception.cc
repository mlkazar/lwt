
// Copyright (c) Microsoft Corporation. All rights reserved.
#include <pthread.h>
#include <unistd.h>

#include "thread.h"
#include "Exception.h"

using namespace std;

Exception::Exception(const std::string &msg,
                     std::string details,
                     bool stacktrace)
    : std::runtime_error(msg),
      _details(details),
      // 2 to avoid emitting the make_unqiue and Exception constructors
      _stacktrace(0, 0)
{
    if (stacktrace)
    {
        // skip the Exception constructor in the backtrace
        _stacktrace =
            boost::stacktrace::stacktrace(1, -1);
    }
}

Exception::Exception(int err, const std::string &msg,
                     std::string details,
                     bool stacktrace)
    : std::runtime_error(msg + ": " + std::string(strerror(err))),
      _details(details),
      _stacktrace(1, 0)
{
    if (stacktrace)
    {
        // skip the Exception constructor in the backtrace
        _stacktrace = boost::stacktrace::stacktrace(1, -1);
    }
}

Exception::~Exception()
{
}

const std::string *Exception::Details()
{
    return &_details;
}

boost::stacktrace::stacktrace *Exception::Stacktrace()
{
    return &_stacktrace;
}

static void
GetExceptionDetailsHelper(ostream &ost,
                          const char *whatp,
                          const std::string *detailsp, boost::stacktrace::stacktrace *stacktracep)
{
    auto currthread = Thread::getCurrent();
    pid_t pthreadId = gettid();
    char pthreadName[64];
    memset(pthreadName, 0, 64);
    pthread_getname_np(pthread_self(), pthreadName, 64);

    ost << "Unhandled Exception: " << whatp << endl;

    ost << "lwt thread " << currthread
        << " (" << currthread->name() << ")" << endl;

    ost << "pthread " << pthreadId
        << " (" << pthreadName << ")" << endl;

    if (detailsp && detailsp->size())
    {
        ost << "Details:" << endl
            << *detailsp << endl;
    }
    if (stacktracep && stacktracep->size())
    {
        ost << "Stacktrace:" << endl
            << *stacktracep << endl;
    }
}

void GetExceptionDetails(ostream &ost,
                         Exception &e)
{
    GetExceptionDetailsHelper(ost, e.what(), e.Details(), e.Stacktrace());
}

void GetExceptionDetails(ostream &ost,
                         std::exception &e)
{
    auto detailsp = boost::get_error_info<ExceptionDetail>(e);
    auto stacktracep = boost::get_error_info<ExceptionStackTrace>(e);

    GetExceptionDetailsHelper(ost, e.what(), detailsp, stacktracep);
}
