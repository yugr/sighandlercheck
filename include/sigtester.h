#ifndef SIGTESTER_H
#define SIGTESTER_H

#include <stdint.h>

extern uintptr_t libc_base, libm_base, libpthread_base;

extern int sigtester_initialized, sigtester_initializing;

void sigtester_init();

void check_context(const char *name, const char *lib);

#endif
