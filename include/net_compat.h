#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <io.h>

#ifndef socklen_t
typedef int socklen_t;
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

#ifndef EINTR
#define EINTR 4
#endif

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

/* Auto-initialize Winsock when process starts */
static void __attribute__((constructor)) __net_compat_init(void) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

static void __attribute__((destructor)) __net_compat_cleanup(void) {
    WSACleanup();
}

static inline int net_init(void) {
    return 0;
}

static inline void net_cleanup(void) {
}

/* Wrapper for inet_pton for older MinGW runtimes */
static inline int compat_inet_pton(int af, const char *src, void *dst) {
    if (af == AF_INET) {
        unsigned long addr = inet_addr(src);
        if (addr == INADDR_NONE && strcmp(src, "255.255.255.255") != 0) {
            return 0;
        }
        ((struct in_addr *)dst)->s_addr = addr;
        return 1;
    }
    return -1;
}
#define inet_pton compat_inet_pton

/* Wrapper for setsockopt to handle POSIX struct timeval for SO_RCVTIMEO */
static inline int compat_setsockopt(SOCKET s, int level, int optname, const void *optval, int optlen) {
    if (level == SOL_SOCKET && optname == SO_RCVTIMEO && optlen == sizeof(struct timeval)) {
        const struct timeval *tv = (const struct timeval *)optval;
        DWORD ms = (DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
        return setsockopt(s, level, optname, (const char *)&ms, sizeof(ms));
    }
    return setsockopt(s, level, optname, (const char *)optval, optlen);
}
#define setsockopt compat_setsockopt

/* Wrapper for sendto with const void* buffer matching POSIX signature */
static inline int compat_sendto(SOCKET s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return sendto(s, (const char *)buf, (int)len, flags, to, tolen);
}
#define sendto compat_sendto

/* Wrapper for recvfrom to map Winsock timeout / error codes to errno */
static inline int compat_recvfrom(SOCKET s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen) {
    int ret = recvfrom(s, (char *)buf, (int)len, flags, from, fromlen);
    if (ret < 0) {
        int werr = WSAGetLastError();
        if (werr == WSAETIMEDOUT || werr == WSAEWOULDBLOCK || werr == WSAECONNRESET) {
            errno = EAGAIN;
        } else if (werr == WSAEINTR) {
            errno = EINTR;
        }
    }
    return ret;
}
#define recvfrom compat_recvfrom

/* Wrapper for close on sockets vs files */
static inline int compat_close(int fd) {
    if (closesocket((SOCKET)fd) == 0) {
        return 0;
    }
    return _close(fd);
}
#define close compat_close

static inline void net_sleep_ms(int ms) {
    Sleep((DWORD)ms);
}

#else /* POSIX systems */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

static inline int net_init(void) { return 0; }
static inline void net_cleanup(void) { }

static inline void net_sleep_ms(int ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

#endif /* _WIN32 */

#endif /* NET_COMPAT_H */
