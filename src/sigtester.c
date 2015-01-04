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

// TODO: print stats at exit (what signals intercepted, etc.)
// TODO: add missing error checks
// TODO: make sure that interceptors respect errno
// TODO: mind thread safety in all functions

// TODO: should this be TLS?
static volatile sig_atomic_t signal_depth = 0;

static inline void push_signal_context() {
  assert(signal_depth >= 0 && "invalid nesting");
  atomic_inc(&signal_depth);
}

static inline void pop_signal_context() {
  atomic_dec(&signal_depth);
  assert(signal_depth >= 0 && "invalid nesting");
}

const char *libc_name = "libc-2.19.so",
  *libm_name = "libm-2.19.so",
  *libpthread_name = "libpthread-2.19.so";
uintptr_t libc_base,
  libm_base,
  libpthread_base;

int sigtester_initialized = 0,
  sigtester_initializing = 0;

void __attribute__((constructor)) sigtester_init() {
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
    if(internal_strstr(buf, libc_name) && !libc_base)
      base = &libc_base;
    else if(internal_strstr(buf, libm_name) && !libm_base)
      base = &libm_base;
    else if(internal_strstr(buf, libpthread_name) && !libpthread_base)
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

struct sigtester_info {
  union {
    sighandler_t h;
    void (*sa)(int, siginfo_t *, void *);
  } user_handler;
  int is_ever_set;
  int active;
  int siginfo;
};

static inline void sigtester_info_clear(volatile struct sigtester_info *si) {
  si->user_handler.h = 0;
  si->active = 0;
}

static volatile struct sigtester_info sigtab[_NSIG];

void check_context(const char *name, const char *lib) {
  //SAY("check_context: ", name, " from ", lib);
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
    const char *sig_str = sys_siglist[i];
    SAY("unsafe call in handler for signal ", signum_str, " (", sig_str, "): ", name, " from ", lib);
  }
}

static void sigtester(int signum) {
  volatile struct sigtester_info *si = &sigtab[signum];
  if(si->siginfo || !si->user_handler.h)
    DIE("received signal but no handler");
  push_signal_context();
  si->active = 1;
  si->user_handler.h(signum);
  pop_signal_context();
  si->active = 0;
}

static void sigtester_sigaction(int signum, siginfo_t *info, void *ctx) {
  volatile struct sigtester_info *si = &sigtab[signum];
  push_signal_context();
  si->active = 1;
  if(si->siginfo) {
    if(!si->user_handler.h)
      DIE("received signal but no handler");
    si->user_handler.h(signum);
  } else {
    if(!si->user_handler.sa)
      DIE("received signal but no handler");
    si->user_handler.sa(signum, info, ctx);
  }
  pop_signal_context();
  si->active = 0;
}

EXPORT sighandler_t signal(int signum, sighandler_t handler) {
  static sighandler_t (*signal_real)(int signum, sighandler_t handler) = 0;
  if(!signal_real) {
    // Non-atomic but who cares?
    signal_real = dlsym(RTLD_NEXT, "signal");
  }

  volatile struct sigtester_info *si = &sigtab[signum];

  if(signum >= 1 && signum < _NSIG
      && handler != SIG_IGN && handler != SIG_DFL && handler != SIG_ERR) {
    si->user_handler.h = handler;
    si->is_ever_set = 1;
    si->siginfo = 0;
    handler = sigtester;
  }

  sighandler_t res = signal_real(signum, handler);
  if(res == SIG_ERR)
    si->user_handler.h = 0;

  return res;
}

EXPORT int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  static int (*sigaction_real)(int signum, const struct sigaction *act, struct sigaction *oldact) = 0;
  if(!sigaction_real) {
    // Non-atomic but who cares?
    sigaction_real = dlsym(RTLD_NEXT, "sigaction");
  }

  volatile struct sigtester_info *si = &sigtab[signum];
  struct sigaction myact;

  int siginfo = (act->sa_flags & SA_SIGINFO) != 0;
  void *handler = siginfo ? (void *)act->sa_sigaction : (void *)act->sa_handler;

  if(act
      && signum >= 1 && signum < _NSIG
      && handler != SIG_IGN && handler != SIG_DFL && handler != SIG_ERR) {
    si->is_ever_set = 1;
    si->siginfo = siginfo;
    if(siginfo)
      si->user_handler.sa = act->sa_sigaction;
    else
      si->user_handler.h = act->sa_handler;

    myact.sa_sigaction = sigtester_sigaction;
    myact.sa_flags = act->sa_flags | SA_SIGINFO;
    myact.sa_mask = act->sa_mask;
    act = &myact;
  }

  int res = sigaction_real(signum, act, oldact);
  if(res != 0)
    si->user_handler.sa = 0;

  return res;
}

DEFINE_ALIAS(EXPORT sighandler_t sysv_signal(int signum, sighandler_t handler), signal);
DEFINE_ALIAS(EXPORT sighandler_t bsd_signal(int signum, sighandler_t handler), signal);

