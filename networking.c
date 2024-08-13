#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "networking.h"
#include "read.h"
#include "sds.h"
#include "sockcompat.h"

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
        if (errno == EWOULDBLOCK && !(ctx->flags & VALKEY_BLOCK) || (errno == EINTR)) {
            return 0;
        } else if(errno == ETIMEDOUT && (ctx->flags & VALKEY_BLOCK)) {
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
        if ((errno == EWOULDBLOCK) && !(ctx->flags & VALKEY_BLOCK) || (errno == EINTR)) {
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
        return VALKEY_ERR;
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
return VALKEY_OK;
}

static int _connectTcp(valkeyContext *ctx, const char *addr, int port,
                       const struct timeval *timeout,
                       const char *source_addr) {
    valkeyFD fd;
    int rv, n;
    char port[6];
    struct addrinfo hints, *servinfo, *bservinfo, *p, *b;
    int blocking = (ctx->flags & VALKEY_BLOCK); 
    int reuseaddr = (ctx->flags & VALKEY_REUSEADDR);
    int reuses = 0;
    long timeout_msec = -1;

    servinfo = NULL;
    ctx->conn_type = CONN_TCP; /* Default connection type is TCP */
    ctx->tcp.port = port;

    /* We need to take possession of the passed parameters to make them
     * reusable for a reconnect.
     * We also carefully check that we don't free the data we already own,
     * as in the case of the reconnect method.
     * 
     * This is a bit ugly, but at least it works and doesn't leak memory.
     */
    if (ctx->tcp.host != addr) {
        free(ctx->tcp.host);

        ctx->tcp.host = strdup(addr);
        if (ctx->tcp.host == NULL) goto oom;
    }

    if (timeout) {
        /* TODO: handle timeout if set. */
    } else {
        free(ctx->conn_timeout);
        ctx->conn_timeout = NULL;
    }

    if (source_addr == NULL) {
        free(ctx->tcp.source_addr);
        ctx->tcp.source_addr = NULL;
    } else if (ctx->tcp.source_addr != source_addr) {
        free(ctx->tcp.source_addr);
        ctx->tcp.source_addr = strdup(source_addr);
    }

    snprintf(_port, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* DNS lookup. To use dual stack, set both flags to prefer both IPv4 and 
     * IPv6. By default, for historical reasons, we try IPv4 first and then
     * try IPv6 only if no IPv4 address was found.
     */
    if (ctx->flags & VALKEY_PREFER_IPV6 && ctx->flags & VALKEY_PREFER_IPV4) {
        hints.ai_family = AF_UNSPEC;
    } else if (ctx->flags & VALKEY_PREFER_IPV6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai = AF_INET;
    }

    rv = getaddrinfo(ctx->tcp.host, _port, &hints, &servinfo);
    if (rv != 0 && hints.ai_family != AF_UNSPEC) {
        /* Try again with a different IP version. */
        hints.ai_family = (hints.ai_family == AF_INET) ? AF_INET6 : AF_INET;
        rv = getaddrinfo(ctx->tcp.host, _port, &hints, &servinfo);
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {

addrretry:
        if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == VALKEY_INVALID_FD)
            continue;
        
        c->fd = s;
        if (setBlocking(ctx, 0) != REDIS_OK)
            goto error;
        
        if (ctx->tcp.source_addr) {
            int bound = 0;
            /* Using getaddrinfo saves us from self-determining IPv4 vs IPv6 */
            if ((rv = getaddrinfo(c->tcp.source_addr, NULL, &hints, &bservinfo)) != 0) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Can't get addr: %s", gai_strerror(rv));
                setError(c, VALKEY_ERR_OTHER, buf);
                goto error;
            }
            
            if (reuseaddr) {
                n = 1;
                if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*) &n, sizeof(n)) < 0) {
                    freeaddrinfo(bservinfo);
                    goto error;
                }
            }

            for (b = bservinfo; b != NULL; b = b->ai_next) {
                if (bind(s, b->ai_addr, b->ai_addrlen) != -1) {
                    bound = 1;
                    break;
                }
            }
            freeaddrinfo(bservinfo);
            if (!bound) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Can't bind socket: %s", strerror(errno));
                setError(ctx, VALKEY_ERR_OTHER, buf);
                goto error;
            }
        }

        /* For repeat connection */
        free(ctx->saddr);
        ctx->saddr = malloc(p->ai_addrlen);
        if (ctx->saddr == NULL)
            goto oom;

        memcpy(ctx->addr, p->ai_addr, p->ai_addrlen);
        ctx->addrlen = p->ai_addrlen;

        if (connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
            if (errno == EHOSTUNREACH) {
                continue;
            } else if (errno == EINPROGRESS) {
                if (blocking) {
                    goto wait_for_ready;
                }
                /* This is ok.
                 * Note that even when it's in blocking mode, we unset blocking
                 * for `connect()`.
                 */
            } else if (errno == EADDRNOTAVAIL && reuseaddr) {
                if(++reuses >= VALKEY_CONNECT_RETRIES) {
                    goto error;
                } else {
                    closeConn(ctx);
                    goto addrretry;
                }
            } else {
                wait_for_ready:
                /* TODO: check if wait is ready. */
                /* TODO: set no delay*/
            }
        }
        if (blocking && setBlocking(ctx, 1) != VALKEY_OK) 
            goto error;

        ctx->flags |= VALKEY_CONNECTED;
        rv = VALKEY_OK;
        goto end;
    }
    if (p == NULL) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Can't create socket %s", strerror(errno));
        setError(ctx, VALKEY_ERR_OTHER, buf);
        goto error;
    }

oom: 
    setError(ctx, VALKEY_ERR_OOM, "Out of memory");
error:
    rv = VALKEY_ERR;
end:
    if (servinfo) {
        freeaddrinfo(servinfo);
    }

    return rv; // Need to return VALKEY_OK if alright
}
