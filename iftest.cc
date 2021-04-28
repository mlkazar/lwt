#include <stdio.h>

#include "ospnet.h"

int
main(int argc, char **argv)
{
    struct sockaddr_in myAddr;
    int32_t code;
    int addrValue;

    code = osp_getNetAddr(&myAddr);
    if (code) {
        printf("code=%d\n", code);
    }
    else {
        addrValue = ntohl(myAddr.sin_addr.s_addr);
        printf("success with ip=%d.%d.%d.%d\n",
               (addrValue>>24) & 0xFF,
               (addrValue>>16) & 0xFF,
               (addrValue>>8) & 0xFF,
               (addrValue & 0xFF));
    }
}
