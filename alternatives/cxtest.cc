#define _XOPEN_SOURCE 1

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>

ucontext_t mainx;
ucontext_t thra;
ucontext_t thrb;
int dispId=0;

class Task {
    ucontext_t _ctx;
    
};

void
dispatch()
{
    static int ctr = 0;
    if ((++ctr) & 1)
        swapcontext(&thra, &thrb);
    else
        swapcontext(&thrb, &thra);
}

void
fn1(int parm, int parm2)
{
    int x;
    while(1) {
        printf("fn1 is %d %d %d with %p\n", sizeof(parm), parm, parm2, &x);
        dispatch();
    }
}

void
setup(ucontext_t *ctxp, int tval)
{
    static const int memSize = 1024*1024;
    getcontext(ctxp);
    ctxp->uc_link = NULL;
    ctxp->uc_stack.ss_sp = malloc(memSize);
    ctxp->uc_stack.ss_size = memSize;
    ctxp->uc_stack.ss_flags = 0;
    makecontext(ctxp, (void (*)()) &fn1, 2, tval, tval+3);
}

int
main(int argc, char **argv)
{
    getcontext(&mainx);
    setup(&thra, 1);
    setup(&thrb, 2);
    swapcontext(&mainx, &thra);
    printf("sleeping....\n");
    while(1) {
        sleep(1);
    }
}
