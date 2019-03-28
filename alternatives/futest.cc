#include <future>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

long long getus()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}

int called_async(int m)
{
    return m+1;
}


int
main(int argc, char **argv)
{
    int i;
    int count;
    int totala;
    int totalb;
    long long start;

    if (argc<2) {
        printf("usage: future <count>\n");
        return -1;
    }

    count = atoi(argv[1]);

    std::future<int> a;
    std::future<int> b;

    totala = 0;
    totalb = 0;

    start = getus();

    for(i=0; i<count; i += 2) {
        a = std::async(called_async, totala);
        b = std::async(called_async, totalb);
        totala = a.get();
        totalb = b.get();
    }

    start = getus() - start;

    printf("ta=%d tb=%d us=%lld ns each\n", totala, totalb, start*1000/count);
}
