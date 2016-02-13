/*
 * Copyright 2015-2016 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#ifndef SIGCHECK_H
#define SIGCHECK_H

#include <stdint.h>

extern uintptr_t libc_base, libm_base, libpthread_base;

extern int sigcheck_initialized, sigcheck_initializing;

void sigcheck_init();

void check_context(const char *name, const char *lib);

#endif
