/*

Copyright 2016-2020 Cazamar Systems

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef __OSP_H_ENV__
#define __OSP_H_ENV__ 1

#include <sys/types.h>
#include <assert.h>

#include <iostream>
#include <boost/stacktrace.hpp>

#define thread_assert(x)                             \
    do                                               \
    {                                                \
        std::cerr << "Stacktrace:" << std::endl      \
                  << boost::stacktrace::stacktrace() \
                  << std::endl;                      \
        assert(x);                                   \
    } while (0)

#define osp_assert(x)                                                  \
    do                                                                 \
    {                                                                  \
        if (!(x))                                                      \
        {                                                              \
            if (osp_assert_finalize_procp)                             \
                osp_assert_finalize_procp(__FILE__, __LINE__);         \
            std::cerr << "Stacktrace:" << std::endl                    \
                      << boost::stacktrace::stacktrace() << std::endl; \
            assert(!(#x));                                             \
        }                                                              \
    } while (0)

#include "ospnew.h"

    typedef void OspAssertFinalizeProc(const char *fileNamep, int lineNumber);

extern long long osp_getUs();

extern long long osp_getMs();

extern OspAssertFinalizeProc *osp_assert_finalize_procp;

#endif /* __OSP_H_ENV__ */
