#include <stdio.h>

#include "ospnet.h"

int32_t
osp_getNetAddr(struct sockaddr_in *myAddrp)
{
    int32_t code;
    struct ifaddrs *ifaddrsp;
    struct sockaddr_in *addrp;
    int32_t rval;
    int sawRealInterface;
    int sawLocalInterface;
    struct sockaddr_in localAddr;
    struct sockaddr_in realAddr;

    code = getifaddrs(&ifaddrsp);
    if (code != 0) {
        return code;
    }

    /* search interfaces for loopback (local) interfaces and non-loopback (real)
     * interfaces, and return the real interface if present, otherwise the
     * local one.
     */
    rval = -1;
    sawRealInterface = 0;
    sawLocalInterface = 0;
    for(;ifaddrsp; ifaddrsp=ifaddrsp->ifa_next) {
        char tbuffer[128];
        addrp = (struct sockaddr_in *)ifaddrsp->ifa_addr;
        if (addrp->sin_family == AF_INET) {
            inet_ntop(AF_INET, &addrp->sin_addr.s_addr, tbuffer, sizeof(tbuffer));
            rval = 0;
            if ( ifaddrsp->ifa_name[0] == 'l' &&
                 ifaddrsp->ifa_name[1] == 'o') {
                memcpy(&localAddr, addrp, sizeof(sockaddr_in));
                sawLocalInterface = 1;
            }
            else {
                memcpy(&realAddr, addrp, sizeof(sockaddr_in));
                sawRealInterface = 1;
            }
        }
    }

    freeifaddrs(ifaddrsp);

    if (sawRealInterface) {
        memcpy(myAddrp, &realAddr, sizeof(*myAddrp));
    }
    else if (sawLocalInterface) {
        memcpy(myAddrp, &localAddr, sizeof(*myAddrp));
    }
    return rval;
}
