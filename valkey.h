#ifndef __VALKEY_H
#define __VALKEY_H
#include "reader.h"

#include <stdint.h>

/* Connection type can be blocking or non-blocking and is set in
 * the least significant bit of the flags field in the valkeyCtx
 */
#define VALKEY_BLOCK (1 << 0)

/* Connection may be disconnected before it is freed. The second
 * bit in the flags field is set when the context is connected.
 */
#define VALKEY_CONNECTED (1 << 1)

/* The async API might try to disconnect gracefully and flush the output
 * buffer and read all the subsequent replies before disconnecting.
 * This flag means no new commands can come in and the connection should
 * be terminated once all the replies have been read.
 */
#define VALKEY_DISCONNECTING (1 << 2)

/* Flag specific to the async API which means that the context should be
 * cleaned up as soon as possible.
 */
 #define VALKEY_FREEING (1 << 3)

/* Flag that is set when an async callback is executed.
 */
#define VALKEY_IN_CALLBACK (1 << 4)

/* Flag that is set when the async context has one or more subscriptions */
#define VALKEY_SUBSCRIBED (1 << 5)

/* Flag that is set when monitor mode is active */
#define VALKEY_MONITORING (1 << 6)

/* Flag that is set when we should send the SO_REUSEADDR before calling bind() */
#define VALKEY_REUSEADDR (1 << 7)

/* Flag that is set when the async connection supports push replies */
#define VALKEY_SUPPORT_PUSH (1 << 8)

/* Flags to prefer IPV4 or IPV6 when doing DNS lookup.(if both are set,
AF_UNSPEC is used) */
#define VALKEY_PREFER_IPV4 (1 << 11)
#define VALKEY_PREFER_IPV6 (1 << 12)

struct valkeyAsyncContext;

/* RESP3 push helpers and callback prototypes */
#define valkeyIsPushReply(r) (((valkeyReply*)(r))->type == REPLY_PUSH)
typedef void (valkeyPushFn)(void *, void *);
typedef void (valkeyAsyncPushFn)(struct valkeyAsyncContext, void *);


typedef struct {
    int type; /* Type of connection to use */
    int options; /* A bit field for REDIS_OPT_xxx */
    const struct timeval *connect_timeout; /* timeout value for connect operations.
                                              If NULL, no timeout is used. */
    const struct timeval *command_timeout; /* timeout value for command operations.
                                              If NULL, no timeout is used. This can
                                              be updated at runtime using valkeySetTimeout
                                              or valkeySetAsyncTimeout. */
    union {
        /* use this field for tcp/ip connections */
        struct {
            const char *source_addr;
            const char *ip;
            int port;
        } tcp;

        /* use this field in case of unix domain sockets */
        const char *unix_socket;

        /* use this field to have valkey-lite operate on an already open file descriptor */
        void *fd;
    } endpoint;

    /* Optional user-defined data/destructors */
    void *privdata;
    void (*free_privdata)(void *);

    /* A user defined PUSH message callback */
    valkeyPushFn *push_callback;
    valkeyAsyncPushFn * async_push_callback;
} valkeyOpts;

typedef struct valkeyReply {
    int type; /* VALKEY_REPLY_* */
    long long integer; /* The integer value when type is VALKEY_REPLY_INTEGER */
    double dval; /* The double value when type is VALKEY_REPLY_DOUBLE */
    size_t len; /* Length of string */
    char *str; /* Used for VALKEY_REPLY_ERROR, VALKEY_REPLY_STRING
                  VALKEY_REPLY_VERB, VALKEY_REPLY_DOUBLE (in additional to dval),
                  and VALKEY_REPLY_BIGNUM. */
    char vtype[4]; /* Used for REDIS_REPLY_VERB, contains the null
                      terminated 3 character content type, such as "txt". */
    size_t elements; /* Number of elements for VALKEY_REPLY_ARRAY */
    struct valkeyReply **element; /* element vector when type is VALKEY_REPLY_ARRAY */
} valkeyReply;

enum ConnectionType {
    TCP,
    UNIX,
    USER_FD
};

typedef struct valkeyContext {
    int err; /* Error flag, set to 0 when there is no error */
    char errstr[128]; /* String representation of the error if it exists */
    void *fd;
    char *buf;

    enum ConnectionType conn_type;

    struct {
        char *host;
        int port;
        char *source_addr;
    } tcp;

    struct {
        char *path;
    } unix_sock;

    /* For non-blocking connections */
    struct sockaddr *saddr;
    size_t addrlen;

    /* Internal context pointer for managing SSL connections */
    void *privateCtx;

    /* An optional RESP3 push handler */
    void *push_callback;
} valkeyContext;

#endif
