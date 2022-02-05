#include <gtest/gtest.h>
#include <pthread.h>
#include <thread>
#include <memory>
#include "thread.h"

class DummyContext {
public:
    void* calledArg;
    int calls;

    DummyContext() : calledArg(NULL), calls(0) {}
    static void* run(void* arg) {
        DummyContext* self = (DummyContext*)(arg);
        self->calledArg = arg;
        self->calls++;
        EXPECT_TRUE(ThreadDispatcher::isLwt());
        return NULL;
    } 
};


TEST(LwtPthread,PthreadBasic)
{
    DummyContext context;
    pthread_t my_pthread;

    int code = pthread_create(&my_pthread,NULL,DummyContext::run,(void*)&context);
    EXPECT_EQ(code,0);

    void* join_retVal;
    code = pthread_join(my_pthread,&join_retVal);
    
    EXPECT_EQ(code,0);
    EXPECT_EQ(join_retVal, (void*)NULL);
    EXPECT_EQ(context.calledArg,&context);
    EXPECT_EQ(context.calls,1);
}

TEST(LwtPthread,StdThreadBasic)
{
    DummyContext context;
    std::thread t(DummyContext::run,(void*)&context);
    t.join();
    EXPECT_EQ(context.calledArg,&context);
    EXPECT_EQ(context.calls,1);
}

TEST(LwtPthread,Many)
{
    const int iterations=1024;
    DummyContext context[iterations];
    std::unique_ptr<std::thread> threads[iterations];

    for(int i=0;i!=iterations;++i) {
        threads[i].reset(new std::thread(DummyContext::run,&context[i]));
    }

    for(int i=0;i!=iterations;++i) {
        threads[i]->join();
    }

    for(int i=0;i!=iterations;++i) {
        EXPECT_EQ(context[i].calledArg,&context[i]);
        EXPECT_EQ(context[i].calls,1);
    }

}