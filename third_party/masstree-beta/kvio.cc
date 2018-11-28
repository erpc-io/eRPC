/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2014 President and Fellows of Harvard College
 * Copyright (c) 2012-2014 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Masstree LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Masstree LICENSE file; the license in that file
 * is legally binding.
 */
// buffered read and write for kvc/kvd.
// stdio is good but not quite what I want.
// need to be able to check if any input
// available, and do non-blocking check.
// also, fwrite just isn't very fast, at
// least on the Mac.

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include "kvio.hh"


// API to allocate a new kvout.
kvout* new_kvout(int fd, int buflen) {
    kvout* kv = (kvout*) malloc(sizeof(kvout));
    assert(kv);
    memset(kv, 0, sizeof(*kv));
    kv->capacity = buflen;
    kv->buf = (char*) malloc(kv->capacity);
    assert(kv->buf);
    kv->fd = fd;
    return kv;
}

// API to allocate a new kvout for a buffer, no fd.
kvout* new_bufkvout() {
    kvout *kv = (kvout*) malloc(sizeof(kvout));
    assert(kv);
    memset(kv, 0, sizeof(*kv));
    kv->capacity = 256;
    kv->buf = (char*) malloc(kv->capacity);
    assert(kv->buf);
    kv->n = 0;
    kv->fd = -1;
    return kv;
}

// API to clear out a buf kvout.
void kvout_reset(kvout* kv) {
    assert(kv->fd < 0);
    kv->n = 0;
}

// API to free a kvout.
// does not close() the fd.
void free_kvout(kvout* kv) {
    if (kv->buf)
        free(kv->buf);
    kv->buf = 0;
    free(kv);
}

void kvflush(kvout* kv) {
    assert(kv->fd >= 0);
    size_t sent = 0;
    while (kv->n > sent) {
        ssize_t cc = write(kv->fd, kv->buf + sent, kv->n - sent);
        if (cc <= 0) {
            if (errno == EWOULDBLOCK) {
                usleep(1);
                continue;
            }
            perror("kvflush write");
            return;
        }
        sent += cc;
    }
    kv->n = 0;
}

// API
void kvout::grow(unsigned want) {
    if (fd >= 0)
        kvflush(this);
    if (want == 0)
        want = capacity + 1;
    while (want > capacity)
        capacity *= 2;
    buf = (char*) realloc(buf, capacity);
    assert(buf);
}

int kvwrite(kvout* kv, const void* buf, unsigned n) {
    if (kv->n + n > kv->capacity && kv->fd >= 0)
        kvflush(kv);
    if (kv->n + n > kv->capacity)
        kv->grow(kv->n + n);
    memcpy(kv->buf + kv->n, buf, n);
    kv->n += n;
    return n;
}
