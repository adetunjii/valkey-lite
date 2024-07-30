#ifndef __NET_H
#define __NET_H

#include "valkey.h"

void close(valkeyContext *ctx); /* Close a network socket */
ssize_t read(valkeyContext *ctx, char *buf, size_t bufcap); /* Reads data from the socket*/
ssize_t write(valkeyContext *ctx);  /* Writes data to the socket */

int checkSocketError(valkeyContext *ctx);
int setTimeout(valkeyContext *ctx, const struct timeval tv);
int connectWithTcp(valkeyContext *ctx, const char *addr, int port, const struct timeval *timeout);
int connectBindTcp(valkeyContext *ctx, const char *addr, int port, const struct timeval *timeout, const char *source_addr);
int connectWithUnix(valkeyContext *ctx, const char *path, const struct timeval *timeout);
int keepAlive(valkeyContext *ctx, int interval);
int checkConnectDone(valkeyContext *ctx, int completed);
int setTcpNoDelay(valkeyContext *ctx);
int setTcpUserTimeout(valkeyContext *ctx, unsigned int timeout);

#endif
