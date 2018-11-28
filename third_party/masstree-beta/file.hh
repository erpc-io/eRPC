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
#ifndef KVDB_FILE_HH
#define KVDB_FILE_HH 1
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include "string.hh"

inline ssize_t
safe_read(int fd, void *buf, size_t count)
{
    size_t pos = 0;
    while (pos != count) {
        ssize_t x = ::read(fd, buf, count - pos);
        if (x != -1 && x != 0) {
            buf = reinterpret_cast<char *>(buf) + x;
            pos += x;
        } else if (x == 0)
            break;
        else if (errno != EINTR && pos == 0)
            return -1;
        else if (errno != EINTR)
            break;
    }
    return pos;
}

inline ssize_t
safe_write(int fd, const void *buf, size_t count)
{
    size_t pos = 0;
    while (pos != count) {
        ssize_t x = ::write(fd, buf, count - pos);
        if (x != -1 && x != 0) {
            buf = reinterpret_cast<const char *>(buf) + x;
            pos += x;
        } else if (x == 0)
            break;
        else if (errno != EINTR && pos == 0)
            return -1;
        else if (errno != EINTR)
            break;
    }
    return pos;
}

inline void
checked_write(int fd, const void *buf, size_t count)
{
    ssize_t x = safe_write(fd, buf, count);
    always_assert(size_t(x) == count);
}

template <typename T> inline void
checked_write(int fd, const T *x)
{
    checked_write(fd, reinterpret_cast<const void *>(x), sizeof(*x));
}


lcdf::String read_file_contents(int fd);
lcdf::String read_file_contents(const char *filename);
int sync_write_file_contents(const char *filename, const lcdf::String &contents,
                             mode_t mode = 0666);
int atomic_write_file_contents(const char *filename, const lcdf::String &contents,
                               mode_t mode = 0666);

#endif
