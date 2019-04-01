#ifndef __OSP_H_ENV__
#define __OSP_H_ENV__ 1

#include <sys/types.h>
#include <assert.h>

#define osp_assert(x) assert(x)

extern long long osp_getUs();

#endif /* __OSP_H_ENV__ */
