#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "networking.h"
#include "read.h"
#include "sds.h"


/* Defined in valkey.c */
void setError(valkeyContext *ctx, int type, const char *str);

ssize_t readNet(valkeyContext *ctx, char *buf, size_t bufcap) {
    ssize_t n = recv(ctx->fd, buf, bufcap, 0);
    if (n == -1) {
        if (errno == EWOULDBLOCK && !(ctx->flag & VALKEY_BLOCK) || (errno == EINTR)) {
            return 0;
        } else if(errno == ETIMEDOUT && (ctx->flag & VALKEY_BLOCK)) {
            setError(ctx, ERR_TIMEOUT, "recv timed out");
            return -1;
        } else {
            setError(ctx, ERR_IO, strerror(errno));
            return -1;
        }
    } else if (n == 0) {
        setError(ctx, ERR_EOF, "Server closed the connection");
        return -1;
    } else {
        return n;
    }
}

ssize_t writeNet(valkeyContext *ctx) {
    ssize_t n;

    n = send(ctx->fd, ctx->out_buf, sdslen(ctx->out_buf), 0);
    if (nwritten < 0) {
        if ((errno == EWOULDBLOCK) && !(ctx->flag & VALKEY_BLOCK) || (errno == EINTR)) {
            /* Try again */
            return 0;
        } else {
            setError(ctx, ERR_IO, strerror(errno));
            return -1;
        }
    }

    return n
}
