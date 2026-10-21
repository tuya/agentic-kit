/* Host socket declarations for compiling the production FreeRTOS PAL. */
#ifndef PAL_TEST_LWIP_SOCKETS_H
#define PAL_TEST_LWIP_SOCKETS_H

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#define lwip_fcntl fcntl

#endif
