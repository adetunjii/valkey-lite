#ifndef __VALKEY_READER_H
#define __VALKEY_READER_H
#include <stdio.h>

#define ERR_IO 0x01 /* Error in read or write */
#define ERR_EOF 0x02 /* End of file */
#define ERR_PROTOCOL 0x03 /* Protocol Error */
#define ERR_TIMEOUT 0x06 /* Connection Timeout */

#define REPLY_PUSH 12

typedef struct valkeyReader {
    int err; /* Error flag, set to 0 when there is no error */
    char errstr[128]; /* String representation of the error if it exists */

    char *buf; /* Read buffer */
    size_t pos; /* Buffer cursor */
    size_t len; /* Buffer length */
    size_t maxbuf; /* Max length of unused buffer */
    long long maxelements; /* Max multi-bulk elements */

    int readIdx; /* Current index of the read task */
    void *reply; /* Temporary reply pointer */

    void *privdata;
} valkeyReader;

#endif
