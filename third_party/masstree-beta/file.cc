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
#include "file.hh"
#include "straccum.hh"
#include <fcntl.h>
#include <stdio.h>

lcdf::String read_file_contents(int fd) {
    lcdf::StringAccum sa;
    while (1) {
        char *buf = sa.reserve(4096);
        if (!buf) {
            errno = ENOMEM;
            return lcdf::String();
        }

        ssize_t x = read(fd, buf, 4096);
        if (x != -1 && x != 0)
            sa.adjust_length(x);
        else if (x == 0)
            break;
        else if (errno != EINTR)
            return lcdf::String();
    }

    errno = 0;
    return sa.take_string();
}

lcdf::String read_file_contents(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
        return lcdf::String();

    lcdf::String text = read_file_contents(fd);

    if (text.empty() && errno) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
    } else
        close(fd);
    return text;
}

int sync_write_file_contents(const char *filename, const lcdf::String &contents,
                             mode_t mode)
{
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, mode);
    if (fd == -1)
        return -1;

    ssize_t x = safe_write(fd, contents.data(), contents.length());
    if (x != contents.length()) {
    error:
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    if (fsync(fd) != 0)
        goto error;

    return close(fd);
}

int atomic_write_file_contents(const char *filename, const lcdf::String &contents,
                               mode_t mode)
{
    lcdf::String tmp_filename = lcdf::String(filename) + ".tmp";
    int r = sync_write_file_contents(tmp_filename.c_str(), contents, mode);
    if (r != 0)
        return -1;

    return rename(tmp_filename.c_str(), filename);
}
