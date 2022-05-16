#include <iostream>
#include <vector>
#include <string>

#include <gtest/gtest.h>

#include <boost/process.hpp>

#include "Exception.h"
#include "thread.h"

using namespace std;

TEST(TestException, Simple)
{
    try
    {
        throw Exception("helloworld");
    }
    catch (const Exception &e)
    {
        EXPECT_EQ(string(e.what()), "helloworld");
    }
}

TEST(TestException, Empty)
{
    try
    {
        throw Exception("");
    }
    catch (const Exception &e)
    {
        EXPECT_EQ(string(e.what()), "");
    }
}

TEST(TestException, Errno)
{
    try
    {
        throw Exception(EPERM, "myerror");
    }
    catch (const Exception &e)
    {
        EXPECT_EQ(string(e.what()), "myerror: Operation not permitted");
    }

    try
    {
        throw Exception(EPERM, "traceme2");
    }
    catch (Exception &e)
    {
        EXPECT_EQ(string(e.what()), "traceme2: Operation not permitted");
        ASSERT_TRUE(e.Stacktrace() != nullptr);
        ASSERT_GT(e.Stacktrace()->size(), 0);
        for (auto &frame : *e.Stacktrace())
        {
            auto start_pos = frame.name().find("TestException");
            EXPECT_EQ(0, start_pos);
            break;
        }
    }
    try
    {
        throw Exception(EPERM, "traceme3", "details3", false);
    }
    catch (Exception &e)
    {
        EXPECT_EQ(string(e.what()), "traceme3: Operation not permitted");
        ASSERT_TRUE(e.Stacktrace() != nullptr);
        ASSERT_EQ(e.Stacktrace()->size(), 0);
    }
}

TEST(TestException, Stacktrace)
{
    try
    {
        throw Exception("traceme1");
    }
    catch (Exception &e)
    {
        EXPECT_EQ(string(e.what()), "traceme1");
        ASSERT_TRUE(e.Stacktrace() != nullptr);
        ASSERT_GT(e.Stacktrace()->size(), 0);
        for (auto &frame : *e.Stacktrace())
        {
            auto start_pos = frame.name().find("TestException");
            EXPECT_EQ(0, start_pos);
            break;
        }
    }
}

TEST(TestException, Details)
{
    try
    {
        throw Exception("detailme1", "this is some additional detail");
    }
    catch (Exception &e)
    {
        EXPECT_EQ(string(e.what()), "detailme1");
        ASSERT_TRUE(e.Details() != nullptr);
        EXPECT_EQ(*e.Details(), "this is some additional detail");
        ASSERT_TRUE(e.Stacktrace() != nullptr);
        ASSERT_GT(e.Stacktrace()->size(), 0);
        for (auto &frame : *e.Stacktrace())
        {
            auto start_pos = frame.name().find("TestException");
            EXPECT_EQ(0, start_pos);
            break;
        }
    }

    try
    {
        throw Exception("detailme2", "this is some additional detail", false);
    }
    catch (Exception &e)
    {
        EXPECT_EQ(string(e.what()), "detailme2");
        ASSERT_TRUE(e.Details() != nullptr);
        EXPECT_EQ(*e.Details(), "this is some additional detail");
        ASSERT_TRUE(e.Stacktrace() != nullptr);
        ASSERT_EQ(e.Stacktrace()->size(), 0);
    }

    try
    {
        throw Exception("detailme3");
    }
    catch (Exception &e)
    {
        EXPECT_EQ(string(e.what()), "detailme3");
        ASSERT_TRUE(e.Details() != nullptr);
        EXPECT_TRUE(e.Details()->empty());
        ASSERT_TRUE(e.Stacktrace() != nullptr);
        ASSERT_GT(e.Stacktrace()->size(), 0);
    }
}

TEST(TestException, Uncaught)
{
    EXPECT_THROW(
        {
            throw Exception("uncaught");
        },
        Exception);
}

TEST(TestException, ExceptionUncaught)
{
    vector<string> exception_cases{"--main", "--lwt", "--pthread"};

    for (auto test_case : exception_cases)
    {
        namespace bp = boost::process;
        bp::ipstream child_cerr;
        auto testchild = bp::child(
            bp::search_path("force_abnormal_exit", {"build/lwt/tests", "lwt/tests"}),
            test_case,
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
        EXPECT_TRUE(output.at(0).find("Unhandled Exception") == 0);
        EXPECT_TRUE(output.at(3).find("Stacktrace") == 0);
        EXPECT_TRUE(output.at(output.size() - 1).find("unhandled") != ios::end);
    }
}