#include "osp.h"
#include <stdio.h>

#include <ctime>
#include <chrono>

long long
osp_getUs()
{
    long long us;
    std::chrono::time_point<std::chrono::high_resolution_clock> now = 
        std::chrono::high_resolution_clock::now();
    us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    return us;
}

long long
osp_getMs() {
    return osp_getUs() / 1000;
}
