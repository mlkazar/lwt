#include <iostream>
#include <vector>
#include <string>

#include <gtest/gtest.h>

#include <boost/process.hpp>

#include "Exception.h"
#include "thread.h"

using namespace std;

TEST(TestAssert, AssertSuccess)
{
    // All of these should not assert
    osp_assert(1);
    thread_assert(1);
    assert(1);
    EXPECT_TRUE(true);
}

TEST(TestAssert, AssertFails)
{
    // test cases are pairs of (test argument, stacktrace_expected)
    vector<pair<string, bool>> assertion_cases{{"--assert", false},
                                               {"--osp_assert", true},
                                               {"--thread_assert", true}};

    for (auto test_case : assertion_cases) {
        auto test_option = test_case.first;
        auto expect_stacktrace = test_case.second;

        namespace bp = boost::process;
        bp::ipstream child_cerr;
        auto testchild = bp::child(
            bp::search_path("force_abnormal_exit", {"build/lwt/tests", "lwt/tests"}),
            test_option,
            bp::std_err > child_cerr);

        vector<string> output;
        string line;
        while (testchild.running() && std::getline(child_cerr, line))
        {
            if (!line.empty())
            {
                output.push_back(line);
            }
        }
        testchild.wait();

        ASSERT_TRUE(testchild.exit_code() == 6);
        if (expect_stacktrace) {
            EXPECT_TRUE(output.at(0).find("Stacktrace") == 0);
        } else {
            EXPECT_FALSE(output.at(0).find("Stacktrace") == 0);
        }
        cout << test_option << " " << output.size() << endl;
        EXPECT_TRUE(output.at(output.size() - 1).find("Assertion") != ios::end);
    }
}