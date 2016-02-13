/*
 * Copyright 2015-2016 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#ifndef LIBC_H
#define LIBC_H

#include <stdlib.h>  // size_t
#include <asm-generic/unistd.h>

static __attribute__((noreturn)) void internal__exit(int status) {
  asm(
    "mov %0, %%rdi\n"
    "mov %1, %%eax\n"
    "syscall\n"
    :: "i"(__NR_exit), "r"(status)
  );
  while(1);
}

static inline size_t internal_strlen(const char *s) {
  size_t n;
  for(n = 0; s && *s; ++n, ++s);
  return n;
}

static inline void *internal_memchr(void *p, char c, size_t n) {
  char *pc = (char *)p;
  size_t i;
  for(i = 0; i < n; ++i) {
    if(pc[i] == c)
      return pc + i;
  }
  return 0;
}

static inline int internal_memcmp(const void *a, const void *b, size_t n) {
  const char *ac = (const char *)a,
    *bc = (const char *)b;
  size_t i;
  for(i = 0; i < n; ++i) {
    if(ac[i] < bc[i])
      return -1;
    else if(ac[i] > bc[i])
      return 1;
  }
  return 0;
}

char *internal_strstr(const char *haystack, const char *needle);

const char *int2str(int value, char *str, size_t size);

#endif
