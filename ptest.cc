#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

pthread_mutex_t main_mutex;
pthread_cond_t main_cond1;
pthread_cond_t main_cond2;
long main_counter;
long main_maxCounter;
long main_startUs;
int main_stopped1 = 0;
int main_stopped2 = 0;
pthread_key_t main_key;
long getus()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000000 + tv.tv_usec;
}

void *
start1(void *ctxp)
{
    pthread_mutex_lock(&main_mutex);
    while(1) {

        main_counter++;
        if (main_counter >= main_maxCounter) {
            long long diff;

            diff = getus() - main_startUs;
            printf("Done, %d RTTs in %lld us (%lld ns each)!\n", main_maxCounter, diff, 
                   diff*1000/main_maxCounter);
            exit(0);
        }

        if (main_stopped2) {
            pthread_cond_broadcast(&main_cond2);
        }

        main_stopped1 = 1;
        pthread_cond_wait(&main_cond1, &main_mutex);
    }

    pthread_mutex_unlock(&main_mutex);
}

void *
start2(void *ctxp)
{
    pthread_mutex_lock(&main_mutex);
    while(1) {
        if (main_stopped1) {
            pthread_cond_broadcast(&main_cond1);
        }

        main_stopped2 = 1;
        pthread_cond_wait(&main_cond2, &main_mutex);
    }

    pthread_mutex_unlock(&main_mutex);
}

int
main(int argc, char **argv)
{
    pthread_t junk;
    long start;
    long i;
    pthread_mutex_t testMutex;
    long diff;
    void *junk2;

    if (argc < 2) {
        printf("usage: ptest <count of round trips to time>\n");
        return 1;
    }

    main_maxCounter = atoi(argv[1]);

    pthread_key_create(&main_key, NULL);
    pthread_setspecific(main_key, &i);
    start = getus();
    for(i=0;i<main_maxCounter;i++) {
        junk2 = pthread_getspecific(main_key);
    }
    diff = getus() - start;
    printf("Getspecific %d calls in %ld ns (%ld ns each)\n",
           main_maxCounter, diff, diff*1000/main_maxCounter);

    pthread_mutex_init(&testMutex, NULL);
    start = getus();
    for(i = 0;i<main_maxCounter; i++) {
        pthread_mutex_lock(&testMutex);
        pthread_mutex_unlock(&testMutex);
    }

    diff = getus() - start;
    printf("Mutex lock/unlock test %d pairs in %ld ns (%ld ns each)\n",
           main_maxCounter, diff, diff*1000/main_maxCounter);

    pthread_mutex_init(&main_mutex, NULL);
    pthread_cond_init(&main_cond1, NULL);
    pthread_cond_init(&main_cond2, NULL);

    main_startUs = getus();
    
    pthread_create(&junk, NULL, start1, NULL);
    pthread_create(&junk, NULL, start2, NULL);

    while(1) {
        sleep(1);
    }

    return 0;
}
