#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include "Exception.h"
#include "thread.h"

// test_abnormal_exit - an integration test for abnormal exit behavior
// --lwt, throws an uncaught Exception in a lwt thread context
//   (with backtrace)
// --main will throw an uncaught Exception in the main loop
//   (with backtrace)
// --pthread will throw an uncaught Exception in a pthread context
//   (with backtrace)
// --assert will assert using the system defined assert
//   (without backtrace)
// --osp_assert will assert using the lwt defined osp_assert
//   (with backtrace)
// --thread_assert will assert using the lwt define thread_assert
//   (with backtrace)
// in all cases, the program will exit due to a SIGABRT

using namespace std;

class DieException : public Exception
{
public:
    DieException() : Exception("imdead") {}
    virtual ~DieException() {}
};

int die()
{
    throw DieException();
    return 0;
}

class DieThread : public std::thread
{
public:
    bool started;
    DieThread() : std::thread(&DieThread::Run, this), started(false) {}
    virtual ~DieThread(){};
    virtual void Run()
    {
        started = true;
        die();
    }
};

class LwtDieThread : public Thread
{
public:
    bool started;
    LwtDieThread() : Thread("LwtDie"), started(false) {}
    virtual ~LwtDieThread() {}
    virtual void *start()
    {
        started = true;
        die();
        return nullptr;
    }
};

int real_main(int argc, char **argv)
{
    bool mainDie = false;
    bool pthreadDie = false;
    bool lwtDie = false;

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 0)
    {
        cout << "Must specify one of --[main|pthread|lwt|assert|osp_assert|thread_assert" << endl;
    }

    for (auto argiter = args.begin(); argiter != args.end(); ++argiter)
    {
        if (*argiter == "--main")
        {
            mainDie = true;
        }
        else if (*argiter == "--pthread")
        {
            pthreadDie = true;
        }
        else if (*argiter == "--lwt")
        {
            lwtDie = true;
        }
        else if (*argiter == "--assert")
        {
            assert("assert" == nullptr);
        }
        else if (*argiter == "--osp_assert")
        {
            osp_assert("osp_assert" == nullptr);
        }
        else if (*argiter == "--thread_assert")
        {
            thread_assert("thread_assert" == nullptr);
        }
        else
        {
            cout << "Unknown argument " << *argiter << endl;
            return 1;
        }
    }

    ThreadDispatcher::pthreadTop();
    ThreadDispatcher::setup(1, 1000);

    if (mainDie)
    {
        die();
    }
    else if (pthreadDie)
    {
        DieThread dieThread;
        while (!dieThread.started)
        {
            usleep(1000);
        }
        dieThread.join();
    }
    else if (lwtDie)
    {
        LwtDieThread lwtDieThread;
        lwtDieThread.setJoinable();
        lwtDieThread.queue();
        lwtDieThread.join(nullptr);
    }
    return 0;
}

int main(int argc, char **argv)
{
    try
    {
        return real_main(argc, argv);
    }
    catch (Exception &e)
    {
        GetExceptionDetails(std::cerr, e);
    }
    catch (std::exception &e)
    {
        GetExceptionDetails(std::cerr, e);
    }
    assert("unhandled exception" == nullptr);
}