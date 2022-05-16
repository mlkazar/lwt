// Copyright (c) Microsoft Corporation. All rights reserved.
#pragma once

#include <string>
#include <ostream>
#include <exception>
#include <stdexcept>

#include <string.h>

#include <boost/stacktrace.hpp>
#include <boost/exception/all.hpp>

typedef boost::error_info<struct tag_ExceptionStackTrace, boost::stacktrace::stacktrace> ExceptionStackTrace;
typedef boost::error_info<struct tag_ExceptionDetail, std::string> ExceptionDetail;

// throw_with_trace will save a backtrace associated with the exception,
// and then throw the exception.  A catch of the exception will allow
// the backtrace to be looked up and processed as appropriate.
//
// Returns:  N/A
// Throws: Exception
//
// example:
// try { throw_with_trace(Exception("")); }
// catch (const Exception &e) {
//    auto st = boost::get_error_info<ExceptionStackTrace>(e);
//    cerr << e.what() << endl;
//    if (st)
//        cerr << *st << endl;
template <class E>
void throw_with_trace(const E &e)
{
    throw boost::enable_error_info(e)
        << ExceptionStackTrace(boost::stacktrace::stacktrace());
}

// throw_with_info will save a details string with the exception,
// and then throw the exception.  A catch of the exception will allow
// the string to be looked up and processed as appropriate.
//
// Returns:  N/A
// Throws: Exception
//
// ex.
// try { throw_with_detail(Exception(""),"additional details"); }
// catch (const Exception &e) {
//    auto st = boost::get_error_info<ExceptionDetail>(e);
//    cerr << e.what() << endl;
//    if (st)
//        cerr << "Details: " << *st << endl;
template <class E>
void throw_with_detail(const E &e, const std::string &details)
{
    throw boost::enable_error_info(e)
        << ExceptionDetail(details);
}

class Exception : public std::runtime_error
{
public:
    Exception(const std::string &msg,
              std::string details = "",
              bool stacktrace = true);

    Exception(int err, const std::string &msg,
              std::string details = "",
              bool stacktrace = true);

    virtual ~Exception();

    virtual const std::string *Details();

    virtual boost::stacktrace::stacktrace *Stacktrace();

private:
    const std::string _details;
    boost::stacktrace::stacktrace _stacktrace;
};

void GetExceptionDetails(std::ostream &ost, Exception &e);

void GetExceptionDetails(std::ostream &ost, std::exception &e);
