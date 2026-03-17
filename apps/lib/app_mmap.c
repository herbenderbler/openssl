/*
 * Copyright 2026 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "apps.h"

#if defined(OPENSSL_SYS_UNIX) && defined(_POSIX_MAPPED_FILES) && _POSIX_MAPPED_FILES > 0

#include <fcntl.h>
#include <sys/stat.h>

int app_mmap_readonly_file(const char *path, size_t expect_len,
    unsigned char **ptr, size_t *mapped_len)
{
    struct stat st;
    size_t sz;
    int fd;
    void *p;

    if (stat(path, &st) != 0)
        return APP_MMAP_ERR_STAT;
    if (st.st_size < 0)
        return APP_MMAP_ERR_SIZE;
    sz = (size_t)st.st_size;
    if ((off_t)sz != st.st_size)
        return APP_MMAP_ERR_SIZE;
    if (sz == 0) {
        *ptr = NULL;
        *mapped_len = 0;
        return APP_MMAP_EMPTY;
    }
    if (expect_len != (size_t)-1 && expect_len != sz)
        return APP_MMAP_ERR_SIZE;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return APP_MMAP_ERR_OPEN;

    p = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    (void)close(fd);

    if (p == MAP_FAILED)
        return APP_MMAP_ERR_MAP;

    *ptr = (unsigned char *)p;
    *mapped_len = sz;
    return APP_MMAP_OK;
}

#endif
