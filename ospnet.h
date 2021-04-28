#ifndef _OSPNET_H_ENV__
#define _OSPNET_H_ENV__ 1

#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>

int32_t osp_getNetAddr(struct sockaddr_in *myAddrp);

#endif /* _OSPNET_H_ENV__ */
