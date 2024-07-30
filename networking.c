#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#include "networking.h"


/* Defined in valkey.c */
void setError(valkeyContext *ctx, int type, const char *str);

ssize_t read(valkeyContext *ctx, char *buf, size_t bufcap) {
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
