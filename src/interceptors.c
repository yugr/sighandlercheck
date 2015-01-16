#include "sigtester.h"

#ifdef __x86_64__
// Notes:
// * it's ok to trash %rax, it's a temp register
#define SYMBOL(name, addr, lib)                  \
  asm(                                           \
    "\n"                                         \
    "  .global " #name "\n"                      \
    "  .type " #name ", @function\n"             \
    "  .text\n"                                  \
    #name ":\n"                                  \
    "  .cfi_startproc\n"                         \
    "  pushq %rdi\n"                             \
    "  .cfi_def_cfa_offset 16\n"                 \
    "  .cfi_offset 7, -16\n"                     \
    "  pushq %rsi\n"                             \
    "  .cfi_def_cfa_offset 24\n"                 \
    "  .cfi_offset 6, -24\n"                     \
    "  pushq %rdx\n"                             \
    "  .cfi_def_cfa_offset 32\n"                 \
    "  .cfi_offset 2, -32\n"                     \
    "  pushq %rcx\n"                             \
    "  .cfi_def_cfa_offset 40\n"                 \
    "  .cfi_offset 1, -40\n"                     \
    "  pushq %r8\n"                              \
    "  .cfi_def_cfa_offset 48\n"                 \
    "  .cfi_offset 8, -48\n"                     \
    "  pushq %r9\n"                              \
    "  .cfi_def_cfa_offset 56\n"                 \
    "  .cfi_offset 9, -56\n"                     \
    "  movl sigtester_initialized(%rip), %eax\n" \
    "  testl %eax, %eax\n"                       \
    "  jne 1f\n"                                 \
    "  call sigtester_init_1\n"                  \
    "1:\n"                                       \
    "  leaq .LC_name_" #name "(%rip), %rdi\n"    \
    "  movq " #lib "_name(%rip), %rsi\n"     \
    "  call check_context\n"                     \
    "  popq %r9\n"                               \
    "  .cfi_def_cfa_offset 48\n"                 \
    "  popq %r8\n"                               \
    "  .cfi_def_cfa_offset 40\n"                 \
    "  popq %rcx\n"                              \
    "  .cfi_def_cfa_offset 32\n"                 \
    "  popq %rdx\n"                              \
    "  .cfi_def_cfa_offset 24\n"                 \
    "  popq %rsi\n"                              \
    "  .cfi_def_cfa_offset 16\n"                 \
    "  popq %rdi\n"                              \
    "  .cfi_def_cfa_offset 8\n"                  \
    "  movq " #lib "_base(%rip), %rax\n"         \
    "  addq $" #addr ", %rax\n"                  \
    "  jmp *%rax\n"                              \
    ".LC_name_" #name ":\n"                      \
    "  .string \"" #name "\"\n"                  \
    ".LC_lib_" #name ":\n"                       \
    "  .string \"" #lib "\"\n"                   \
    "  .cfi_endproc\n"                           \
    "  .size " #name ", .-" #name "\n"           \
  );
#else
#error Unsupported platform
#endif
#include "all_libc_syms.def"


