#include <gtest/gtest.h>
#include "thread.h"

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    ThreadDispatcher::pthreadTop();
    return RUN_ALL_TESTS();
}