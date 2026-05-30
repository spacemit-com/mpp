/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "socket_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOCKET_POLL_TIMEOUT_MAX_MS 2147483647U

static int socket_poll_timeout_ms(U32 u32TimeoutMs) {
    if (u32TimeoutMs > SOCKET_POLL_TIMEOUT_MAX_MS) {
        return (int)SOCKET_POLL_TIMEOUT_MAX_MS;
    }
    return (int)u32TimeoutMs;
}

S32 Socket_TcpConnect(const CHAR *pszHost, U16 u16Port, U32 u32TimeoutMs) {
    struct addrinfo hints, *res, *rp;
    CHAR szPort[8];
    S32 s32Fd = -1;
    S32 s32Flags;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(szPort, sizeof(szPort), "%u", u16Port);

    if (getaddrinfo(pszHost, szPort, &hints, &res) != 0) {
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        s32Fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s32Fd < 0) {
            continue;
        }

        /* Set non-blocking for connect timeout. If F_GETFL fails it returns
         * -1; using that value with F_SETFL would set bogus flags and mask the
         * real failure, so close this socket and try the next address. */
        s32Flags = fcntl(s32Fd, F_GETFL, 0);
        if (s32Flags < 0 || fcntl(s32Fd, F_SETFL, s32Flags | O_NONBLOCK) < 0) {
            close(s32Fd);
            s32Fd = -1;
            continue;
        }

        if (connect(s32Fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        if (errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = s32Fd;
            pfd.events = POLLOUT;

            if (poll(&pfd, 1, socket_poll_timeout_ms(u32TimeoutMs)) > 0) {
                S32 s32Err = 0;
                socklen_t len = sizeof(s32Err);
                if (getsockopt(s32Fd, SOL_SOCKET, SO_ERROR, &s32Err, &len) == 0 &&
                    s32Err == 0) {
                    break;
                }
            }
        }

        close(s32Fd);
        s32Fd = -1;
    }

    freeaddrinfo(res);

    if (s32Fd >= 0) {
        /* Restore blocking mode. fcntl(F_GETFL) can fail (e.g. EBADF); using
         * its -1 return directly in (s32Flags & ~O_NONBLOCK) would feed a
         * bogus flag set to F_SETFL and leave the socket in an undefined
         * blocking state, so validate both calls and fail the connect on
         * error rather than returning a half-configured fd. */
        s32Flags = fcntl(s32Fd, F_GETFL, 0);
        if (s32Flags < 0 ||
            fcntl(s32Fd, F_SETFL, s32Flags & ~O_NONBLOCK) < 0) {
            close(s32Fd);
            return -1;
        }

        /* Enable TCP_NODELAY */
        S32 s32One = 1;
        setsockopt(s32Fd, IPPROTO_TCP, TCP_NODELAY, &s32One, sizeof(s32One));
    }

    return s32Fd;
}

S32 Socket_UdpBind(U16 u16LocalPort) {
    S32 s32Fd;
    struct sockaddr_in addr;
    S32 s32One = 1;

    s32Fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s32Fd < 0) {
        return -1;
    }

    setsockopt(s32Fd, SOL_SOCKET, SO_REUSEADDR, &s32One, sizeof(s32One));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(u16LocalPort);

    if (bind(s32Fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s32Fd);
        return -1;
    }

    return s32Fd;
}

S32 Socket_SetNonBlocking(S32 s32Fd, BOOL bNonBlock) {
    S32 s32Flags = fcntl(s32Fd, F_GETFL, 0);
    if (s32Flags < 0) {
        return -1;
    }

    if (bNonBlock) {
        s32Flags |= O_NONBLOCK;
    } else {
        s32Flags &= ~O_NONBLOCK;
    }

    return fcntl(s32Fd, F_SETFL, s32Flags);
}

S32 Socket_SetRecvTimeout(S32 s32Fd, U32 u32TimeoutMs) {
    struct timeval tv;
    tv.tv_sec = u32TimeoutMs / 1000;
    tv.tv_usec = (u32TimeoutMs % 1000) * 1000;
    return setsockopt(s32Fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

S32 Socket_SetSendTimeout(S32 s32Fd, U32 u32TimeoutMs) {
    struct timeval tv;
    tv.tv_sec = u32TimeoutMs / 1000;
    tv.tv_usec = (u32TimeoutMs % 1000) * 1000;
    return setsockopt(s32Fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

S32 Socket_SetRecvBufSize(S32 s32Fd, U32 u32Size) {
    return setsockopt(s32Fd, SOL_SOCKET, SO_RCVBUF, &u32Size, sizeof(u32Size));
}

S32 Socket_SendAll(S32 s32Fd, const U8 *pu8Data, U32 u32Size) {
    U32 u32Sent = 0;

    while (u32Sent < u32Size) {
        /* MSG_NOSIGNAL: suppress SIGPIPE when the peer has closed the
         * connection; send() returns -1/EPIPE instead of killing the
         * process. */
        ssize_t n = send(s32Fd, pu8Data + u32Sent, u32Size - u32Sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        u32Sent += (U32)n;
    }

    return (S32)u32Sent;
}

S32 Socket_RecvTimeout(S32 s32Fd, U8 *pu8Buf, U32 u32Size, U32 u32TimeoutMs) {
    struct pollfd pfd;
    pfd.fd = s32Fd;
    pfd.events = POLLIN;

    while (1) {
        S32 s32Ret = poll(&pfd, 1, socket_poll_timeout_ms(u32TimeoutMs));
        if (s32Ret < 0 && errno == EINTR) {
            continue;
        }
        if (s32Ret <= 0) {
            return s32Ret; /* 0=timeout, -1=error */
        }

        ssize_t n = recv(s32Fd, pu8Buf, u32Size, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return (S32)n;
    }
}

S32 Socket_RecvExact(S32 s32Fd, U8 *pu8Buf, U32 u32Size, U32 u32TimeoutMs) {
    U32 u32Recv = 0;

    while (u32Recv < u32Size) {
        S32 n = Socket_RecvTimeout(s32Fd, pu8Buf + u32Recv, u32Size - u32Recv, u32TimeoutMs);
        if (n <= 0) {
            return -1;
        }
        u32Recv += (U32)n;
    }

    return 0;
}

S32 Socket_ReadLine(S32 s32Fd, CHAR *pszBuf, U32 u32MaxLen, U32 u32TimeoutMs) {
    U32 u32Pos = 0;

    /*
     * A zero-length (or NULL) buffer has no room even for the terminating
     * NUL. Reject it explicitly: otherwise "u32MaxLen - 1" would wrap around
     * to UINT32_MAX (unsigned underflow) and the loop would run essentially
     * forever while writing past the caller's buffer.
     */
    if (!pszBuf || u32MaxLen == 0) {
        return -1;
    }

    while (u32Pos < u32MaxLen - 1) {
        S32 n = Socket_RecvTimeout(s32Fd, (U8 *)&pszBuf[u32Pos], 1, u32TimeoutMs);
        if (n <= 0) {
            return -1;
        }

        if (pszBuf[u32Pos] == '\n') {
            u32Pos++;
            break;
        }
        u32Pos++;
    }

    pszBuf[u32Pos] = '\0';
    return (S32)u32Pos;
}

VOID Socket_Close(S32 s32Fd) {
    if (s32Fd >= 0) {
        close(s32Fd);
    }
}

U16 Socket_GetLocalPort(S32 s32Fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getsockname(s32Fd, (struct sockaddr *)&addr, &len) < 0) {
        return 0;
    }

    return ntohs(addr.sin_port);
}

S32 Socket_ResolveHost(const CHAR *pszHost, CHAR *pszIpOut, U32 u32IpBufLen) {
    struct addrinfo hints, *res;

    if (!pszHost || !pszIpOut || u32IpBufLen == 0) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(pszHost, NULL, &hints, &res) != 0) {
        return -1;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    /* inet_ntop returns NULL (errno ENOSPC) when u32IpBufLen is too small to
     * hold the textual address; the output buffer is then undefined. Treat
     * that as a failure instead of reporting success with garbage. */
    if (inet_ntop(AF_INET, &addr->sin_addr, pszIpOut, u32IpBufLen) == NULL) {
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return 0;
}
