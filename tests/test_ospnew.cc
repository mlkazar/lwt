#include <gtest/gtest.h>
#include "ospnew.h"

TEST(OspTestNew,Basic)
{
    struct Foo1  { int data[1]; };

    struct Foo32 { int data[32]; };

    struct Foo4G { int data[/* 4*1024*1024*1024*/ 4294967296]; };

    auto *pFoo1 = new Foo1();
    auto *pFoo32 = new Foo32();
    auto *pFoo4G = new Foo4G();
    EXPECT_TRUE(pFoo1!=NULL);
    EXPECT_TRUE(pFoo32!=NULL);
    EXPECT_TRUE(pFoo4G!=NULL);

    delete pFoo1;
    delete pFoo32;
    delete pFoo4G;

}

TEST(OspTestNew,DeleteNull)
{
    struct Foo {
        int data[32];
        static Foo* getNull() {
            return NULL;
        }
    };

    //The compiler seems to ignore deletion of a NULL if it can figure out it's NULL,
    // so the above is to ensure that delete with null is tested
    Foo* pTest= Foo::getNull();
    delete pTest;
}
