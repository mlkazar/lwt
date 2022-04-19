#include <gtest/gtest.h>
#include "ospnew.h"

//256M entries
#define LARGE_MEM_ENTRIES (268435456ULL)

TEST(OspTestNew,Basic)
{
    struct Foo1  { int data[1]; };

    struct Foo32 { int data[32]; };

    struct Foo1G { int data[LARGE_MEM_ENTRIES]; };

    auto *pFoo1 = new Foo1();
    auto *pFoo32 = new Foo32();
    auto *pFoo1G = new Foo1G();

    pFoo1->data[0] = 0;
    pFoo32->data[31] = 31;
    pFoo32->data[0] = 32;
    pFoo1G->data[LARGE_MEM_ENTRIES-1] = LARGE_MEM_ENTRIES-1;
    pFoo1G->data[0] = LARGE_MEM_ENTRIES;
    
    EXPECT_TRUE(pFoo1!=NULL);
    EXPECT_EQ(pFoo1->data[0],0);

    EXPECT_TRUE(pFoo32!=NULL);
    EXPECT_EQ(pFoo32->data[0],32);
    EXPECT_EQ(pFoo32->data[31],31);
    
    EXPECT_TRUE(pFoo1G!=NULL);
    EXPECT_EQ(pFoo1G->data[0],LARGE_MEM_ENTRIES);
    EXPECT_EQ(pFoo1G->data[LARGE_MEM_ENTRIES-1],LARGE_MEM_ENTRIES-1);
    
    delete pFoo1;
    delete pFoo32;
    delete pFoo1G;
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
