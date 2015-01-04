#ifndef COMPILER_H
#define COMPILER_H

#ifdef __GNUC__
#define ALIGN(n) __attribute__((aligned(n)))
#define EXPORT __attribute__((visibility("default")))
#define UNUSED __attribute__((unused))
#define USED __attribute__((used))
#else
#error Unknown compiler
#endif

#endif

