#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include "networking.h"
#include "read.h"
#include "sds.h"


/* Defined in valkey.c */
void setError(valkeyContext *ctx, int type, const char *str);

void closeConn(valkeyContext *ctx) {
    if (ctx && ctx->fd != VALKEY_INVALID_FD) {
        close(ctx->fd);
        ctx->fd = VALKEY_INVALID_FD;
    }
}

ssize_t readConn(valkeyContext *ctx, char *buf, size_t bufcap) {
    ssize_t n = recv(ctx->fd, buf, bufcap, 0);
    if (n == -1) {
        if (errno == EWOULDBLOCK && !(ctx->flag & VALKEY_BLOCK) || (errno == EINTR)) {
            return 0;
        } else if(errno == ETIMEDOUT && (ctx->flag & VALKEY_BLOCK)) {
            setError(ctx, VALKEY_ERR_TIMEOUT, "recv timed out");
            return -1;
        } else {
            setError(ctx, VALKEY_ERR_IO, strerror(errno));
            return -1;
        }
    } else if (n == 0) {
        setError(ctx, VALKEY_ERR_EOF, "Server closed the connection");
        return -1;
    } else {
        return n;
    }
}

ssize_t writeConn(valkeyContext *ctx) {
    ssize_t n;

    n = send(ctx->fd, ctx->out_buf, sdslen(ctx->out_buf), 0);
    if (n < 0) {
        if ((errno == EWOULDBLOCK) && !(ctx->flag & VALKEY_BLOCK) || (errno == EINTR)) {
            /* Try again */
            return 0;
        } else {
            setError(ctx, VALKEY_ERR_IO, strerror(errno));
            return -1;
        }
    }

    return n
}

static void setErrorFromErrorNo(valkeyContext *ctx, int type, const char *prefix) {
    int errno = errno; 
    char buf[128] = { 0 };
    size_t len = 0;

    if (prefix != NULL) {
        len = snprintf(buf, sizeof(buf), "%s: ", prefix);
    }
    strerror_r(errno, (char*)(buf+len), sizeof(buf) - len);
    setError(ctx, type, buf);
}

static int setReuseAddr(valkeyContext *ctx) {
    int on = 1;
    if(setsockopt(ctx->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        setErrorFromErrorNo(ctx, VALKEY_ERR_IO, NULL);
        closeConn(ctx);
        return VALKEY_ERR
    }

    return VALKEY_OK;
}

static int createConn(valkeyContext *ctx, int type) {
    valkeyFD fd;
    if ((fd = socket(type, SOCK_STREAM, 0)) == VALKEY_INVALID_FD) {
        setErrorFromErrorNo(ctx, VALKEY_ERR_IO, NULL);
        return VALKEY_ERR;
    }
    ctx->fd = fd;
    if (type == AF_INET) {
        if (setReuseAddr(ctx) == VALKEY_ERR) {
            return VALKEY_ERR;
        }
    } 

    return VALKEY_OK;
}

static int setBlocking(valkeyContext *ctx, int blocking) {
#ifndef _WIN32
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    if ((flags = fcntl(ctx->fd, F_GETFL)) < 0) {
        setErrorFromErrorNo(ctx, VALKEY_ERR_IO, "fcntl(F_GETFL)");
        closeConn(ctx);
        return VALKEY_ERR;
    }

    if (blocking) {
        flags &= ~O_NONBLOCK; // clear NONBLOCKING bits.
    } else {
        flags |= O_NONBLOCK;
    }

#else
    u_long mode = blocking ? 0 : 1;
    if (ioctl(ctx->fd, FIONBIO, mode) < 0) {
        setErrorFromErrorNo(ctx, VALKEY_ERR_IO, "ioctl(FIONBIO)");
        closeConn(ctx);
        return VALKEY_ERR;
    }

    return VALKEY_OK;
#endif /* _WIN32 */
}
