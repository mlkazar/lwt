#include <gtest/gtest.h>
#include "thread.h"

class TestRunner : public Thread {
public:
    TestRunner() : status(-1) {}

    virtual void* start()
    {
        status = RUN_ALL_TESTS();
        return nullptr;
    }

    int Status() { return status; }

private:
    int status;
};

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // Run all the tests in an lwt thread as well as "normal" pthread 
    // to ensure that the exception handling and stack tracing are tested with lwt threads
    ThreadDispatcher::setup(1, 1000);

    auto test = new TestRunner();
    test->setJoinable();
    test->queue();
    test->join(nullptr);
    return test->Status();
}