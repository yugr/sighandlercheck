#define _GNU_SOURCE

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

#include "sigtester.h"
#include "libc.h"
#include "compiler.h"

#define WRITE_LIT(fd, msg) do { \
    ssize_t UNUSED n = write((fd), (msg), sizeof(msg)); \
  } while(0)

#define WRITE_PTR(fd, p) do { \
    size_t _len = internal_strlen(p); \
    ssize_t UNUSED _n = write((fd), (p), _len); \
  } while(0)

#define WRITE(fd, ...) do { \
    const char *_ss[] = { "",##__VA_ARGS__, 0 }, **_p; \
    for(_p = &_ss[0]; *_p; ++_p) { \
      WRITE_PTR(STDERR_FILENO, *_p); \
    } \
  } while(0)

#define SAY(...) WRITE(STDERR_FILENO, "sigtester: ",##__VA_ARGS__, "\n")

// TODO: syscall exit?
#define DIE(...) do { \
    SAY("",##__VA_ARGS__, "; aborting..."); \
    while(1); \
  } while(0)

static volatile sig_atomic_t signal_depth = 0;

static inline void enter_signal_context() {
  // TODO: atomicity? Should this be TLS?
  assert(signal_depth >= 0 && "invalid nesting");
  ++signal_depth;
}

static inline void exit_signal_context() {
  --signal_depth;
  assert(signal_depth >= 0 && "invalid nesting");
}

uintptr_t libc_base, libm_base, libpthread_base;

int sigtester_initialized = 0,
  sigtester_initializing = 0;

// TODO: what about errno?
void sigtester_init() {
  assert(!sigtester_initializing && "recursive init");
  sigtester_initializing = 1;

  // Find base addresses of intercepted libs

  int fd = open("/proc/self/maps", O_RDONLY);
  if(fd < 0) {
      DIE("failed to open /proc/self/maps");
  }

  char buf[256];
  size_t end = 0, nread;
  while((nread = read(fd, &buf[end], sizeof(buf) - end)) > 0) {
    char *nl = internal_memchr(buf, '\n', sizeof(buf));

    *nl = 0;

    uintptr_t *base = 0;
    if(internal_strstr(buf, "libc-2.19.so") && !libc_base)
      base = &libc_base;
    else if(internal_strstr(buf, "libm-2.19.so") && !libm_base)
      base = &libm_base;
    else if(internal_strstr(buf, "libpthread-2.19.so") && !libpthread_base)
      base = &libpthread_base;

    if(base) {
      // 7f438376c000-7f4383928000 r-xp 00000000 08:01 659498    /lib/x86_64-linux-gnu/libc-2.19.so
      uintptr_t v = 0;
      size_t i;
      for(i = 0; buf[i]; ++i) {
        char c = buf[i];
        if(c >= '0' && c <= '9')
          v = v * 16 + (c - '0');
        else if(c >= 'a' && c <= 'f')
          v = v * 16 + 10 + (c - 'a');
        else if(c >= 'A' && c <= 'F')
          v = v * 16 + 10 + (c - 'A');
        else  // '-'
          break;
      }
      *base = v;
    }

    // Copy start of next line
    size_t i, end_new;
    for(i = nl - buf + 1, end_new = 0; i < end + nread; ++i, ++end_new)
      buf[end_new] = buf[i];
    end = end_new;
  }

  close(fd);

  if(!libc_base) {
     DIE("libc not found");
  }
  if(!libm_base) {
     DIE("libm not found");
  }
  if(!libpthread_base) {
     DIE("libpthread not found");
  }

  sigtester_initialized = 1;
  sigtester_initializing = 0;
}

struct signal_info {
  sighandler_t user_handler;
  int is_ever_set;
  int active;
  // TODO: type (sigaction or signal)
};

static inline void signal_info_clear(volatile struct signal_info *si) {
  si->user_handler = 0;
  si->active = 0;
}

static volatile struct signal_info sigtab[_NSIG];

void check_context(const char *name, const char *lib) {
  SAY("check_context: ", name, " from ", lib);
  if(!signal_depth)
    return;
  // Find active signals
  int i;
  for(i = 0; i < _NSIG; ++i) {
    if(!sigtab[i].active)
     continue;
    char buf[128];
    const char *signum_str = int2str(i, buf, sizeof(buf));
    if(!signum_str)
      DIE("increase buffer size in check_context");
    SAY("unsafe call in signal handler for ", signum_str, ": ", name, " from ", lib);
  }
}

void sigtester(int signum) {
  volatile struct signal_info *si = &sigtab[signum];
  if(si->user_handler) {
    // sigtab[signum].user_handler = 0;  Assume SysV semantics?
    enter_signal_context();
    si->active = 1;
    si->is_ever_set = 1;
    si->user_handler(signum);
    exit_signal_context();
    si->active = 0;
    --signal_depth;
  } else {
    DIE("received signal but no handler");
  }
}

sighandler_t signal(int signum, sighandler_t handler) {
  static sighandler_t (*signal_real)(int signum, sighandler_t handler) = 0;
  if(!signal_real) {
    // Non-atomic but who cares?
    signal_real = dlsym(RTLD_NEXT, "signal");
  }

  if(signum >= _NSIG) {
    DIE("signal out of bounds");
  }

  volatile struct signal_info *si = &sigtab[signum];
  if(handler != SIG_IGN && handler != SIG_DFL) {
    si->user_handler = handler;
    handler = sigtester;
  } else {
    signal_info_clear(si);
  }

  sighandler_t res = signal_real(signum, handler);
  if(res == SIG_ERR) {
    signal_info_clear(si);
  }
  return res;
}

#if 0
TODO: int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
TODO: print stats at exit (what signals intercepted, etc.)
#endif

