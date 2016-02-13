/*
 * Copyright 2015-2016 Yury Gribov
 * 
 * Use of this source code is governed by MIT license that can be
 * found in the LICENSE.txt file.
 */

#ifndef COMPILER_H
#define COMPILER_H

#ifdef __GNUC__

#define ALIGN(n) __attribute__((aligned(n)))
#define EXPORT __attribute__((visibility("default")))
#define UNUSED __attribute__((unused))
#define USED __attribute__((used))
#define DEFINE_ALIAS(decl, target) decl __attribute__((alias(#target)));

#define atomic_inc(p) __sync_fetch_and_add(p, 1)
#define atomic_dec(p) __sync_fetch_and_add(p, -1)

#else
#error Unknown compiler
#endif

#endif

