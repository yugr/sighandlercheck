#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "sigcheck.h"
#include "libc.h"
#include "compiler.h"

static int sigcheck_fd = -1;

int sigcheck_initialized = 0,
  sigcheck_initializing = 0;

// TODO: better verbose prints.
// TODO: make this a function?

#define WRITE(...) do { \
    const char *_ss[] = { "",##__VA_ARGS__, 0 }, **_p; \
    for(_p = &_ss[0]; *_p; ++_p) { \
      int _fd = sigcheck_fd >= 0 ? sigcheck_fd : STDERR_FILENO; \
      size_t _len = internal_strlen(*_p); \
      ssize_t UNUSED _n = write(_fd, *_p, _len); \
    } \
  } while(0)

#define SAY_START(...) do { \
    char buf[128]; \
    const char *pid = sigcheck_initialized ? int2str((int)getpid(), buf, sizeof(buf)) : "???"; \
    WRITE("sigcheck (pid ", pid, "): ",##__VA_ARGS__); \
  } while(0)
#define SAY(...) WRITE(__VA_ARGS__)

// TODO: syscall exit?
#define DIE(...) do { \
    SAY_START("internal error: ",##__VA_ARGS__, "\n"); \
    while(1); \
  } while(0)

// TODO: add missing error checks
// TODO: mind thread safety in all functions

static volatile int signal_depth = 0;
static volatile int num_errors = 0;

static int verbose = 0;
static int max_errors = -1;

enum fork_tests_mode {
  FORK_TESTS_NONE,
  FORK_TESTS_ATEXIT,
  FORK_TESTS_ONSET
};

static enum fork_tests_mode fork_tests =
  FORK_TESTS_NONE;

static inline void push_signal_context(void) {
  assert(signal_depth >= 0 && "invalid nesting");
  atomic_inc(&signal_depth);
}

static inline void pop_signal_context(void) {
  atomic_dec(&signal_depth);
  assert(signal_depth >= 0 && "invalid nesting");
}

#define LIBRARY(name, filename) \
  uintptr_t name ## _base; \
  const char * name ## _name = filename;
#include "all_libc_libs.def"

// This initializes data for interceptors so that libc can work
void __attribute__((constructor)) sigcheck_init_1(void) {
  assert(!sigcheck_initializing && "recursive init");
  sigcheck_initializing = 1;

  // Find base addresses of intercepted libs

  // TODO: sadly we can't check errno for EINTR (it's not yet initialized);
  // one option is replacing all libc wrapper with internal_syscall.
  // Actually this may not be a problem, as signals are not delivered during
  // syscall in modern kernels (?).
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
#define LIBRARY(name, filename) \
  if(internal_strstr(buf, filename) && !name ## _base) \
    base = &name ## _base; \
  else
#include "all_libc_libs.def"
    {}

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

#define LIBRARY(name, filename) \
  if(!name ## _base) DIE(#name " not found");
#include "all_libc_libs.def"

  sigcheck_initialized = 1;
  sigcheck_initializing = 0;
}

static void sigcheck_finalize(void);

// This performs the rest of initialization
void __attribute__((constructor)) sigcheck_init_2(void) {
  if(!sigcheck_initialized)
    sigcheck_init_1();

  char *sigcheck_fd_ = getenv("SIGCHECK_OUTPUT_FILENO");
  if(sigcheck_fd_)
    sigcheck_fd = atoi(sigcheck_fd_);

  char *verbose_ = getenv("SIGCHECK_VERBOSE");
  if(verbose_)
    verbose = atoi(verbose_);

  char *max_errors_ = getenv("SIGCHECK_MAX_ERRORS");
  if(max_errors_)
    max_errors = atoi(max_errors_);

  char *fork_tests_ = getenv("SIGCHECK_FORK_TESTS");
  if(!fork_tests_ || 0 == strcmp(fork_tests_, "none"))
    fork_tests = FORK_TESTS_NONE;
  else if(0 == strcmp(fork_tests_, "atexit"))
    fork_tests = FORK_TESTS_ATEXIT;
  else if(0 == strcmp(fork_tests_, "onset"))
    fork_tests = FORK_TESTS_ONSET;
  else
    DIE("unknown value for SIGCHECK_FORK_TESTS: ", fork_tests_);

  // TODO: can we force this to be run _before_ all other handlers?
  atexit(sigcheck_finalize);
}

struct sigcheck_info {
  union {
    sighandler_t h;
    void (*sa)(int, siginfo_t *, void *);
  } user_handler;
  int is_handled;
  int active;
  int siginfo;
};

static inline void sigcheck_info_clear(volatile struct sigcheck_info *si) {
  si->user_handler.h = 0;
  si->active = 0;
}

static volatile struct sigcheck_info sigtab[_NSIG];

static int is_deadly_signal(int signum) {
  switch(signum) {
  case SIGSEGV:
  case SIGBUS:
  case SIGABRT:
  case SIGILL:
  case SIGTERM:
    return 1;
  default:
    return 0;
  }
}

static int is_interesting_signal(int signum) {
  switch(signum) {
  // TODO: I mainly disabled these to get bash working;
  // perhaps enable these at some point.
  case SIGCHLD:
  case SIGCONT:
  case SIGSTOP:
  case SIGTSTP:
  case SIGTTIN:
  case SIGTTOU:
    return 0;
  default:
    return 1;
  }
}

static void about_signal(int signum) {
  char buf[128];
  const char *signum_str = int2str(signum, buf, sizeof(buf));
  if(!signum_str)
    DIE("increase buffer size in about_signal");
  const char *sig_str = sys_siglist[signum];
  SAY_START("signal ", signum_str, " (", sig_str, "): ");
}

void fork_test(int signum) {
  pid_t pid = fork();
  if(pid < 0) {
    DIE("failed to fork test process");
  } else if(pid == 0) {
    if(verbose) {
      about_signal(signum);
      SAY("sending in forked process\n");
    }
    raise(signum);
    _exit(0);
  } else {
    if(waitpid(pid, 0, 0) < 0)
      DIE("failed to wait for forked test process");
  }
}

static void sigcheck_finalize(void) {
  int i;
  for(i = 0; i < _NSIG; ++i) {
    if(!sigtab[i].is_handled)
      continue;
    if(verbose) {
      about_signal(i);
      SAY("is handled\n");
    }
    if(fork_tests == FORK_TESTS_ATEXIT && is_interesting_signal(i))
      fork_test(i);
  }
}

static int do_report_error() {
  if(max_errors < 0)
    return 1;
  if(num_errors >= max_errors)
    return 0;
  return atomic_inc(&num_errors) >= max_errors;
}

void check_context(const char *name, const char *lib) {
  if(verbose >= 2)
    SAY_START("check_context: ", name, " from ", lib, "\n");
  if(!signal_depth)
    return;
  // For all active signals
  int i;
  for(i = 0; i < _NSIG; ++i) {
    if(!sigtab[i].active)
     continue;
    if(do_report_error()) {
      about_signal(i);
      SAY("unsafe call to ", name, " from ", lib, " in user handler\n");
    }
  }
}

#define BAD_ERRNO 0x12345

static void sigcheck(int signum, siginfo_t *info, void *ctx) {
  volatile struct sigcheck_info *si = &sigtab[signum];
  if(!si->user_handler.h)
    DIE("received signal but no handler");
  push_signal_context();
  si->active = 1;
  int old_errno = errno;
  // Check that errno is preserved
  if(!is_deadly_signal(signum))
    errno = BAD_ERRNO;
  if(si->siginfo) {
    si->user_handler.sa(signum, info, ctx);
  } else {
    si->user_handler.h(signum);
  }
  if(errno != BAD_ERRNO && do_report_error()) {
    about_signal(signum);
    SAY("errno not preserved in user handler\n");
  }
  errno = old_errno;
  si->active = 0;
  pop_signal_context();
}

// TODO: factor out common code from signal() and sigaction()
EXPORT sighandler_t signal(int signum, sighandler_t handler) {
  static sighandler_t (*signal_real)(int signum, sighandler_t handler) = 0;
  if(!signal_real) {
    // Non-atomic but who cares?
    signal_real = dlsym(RTLD_NEXT, "signal");
  }

  volatile struct sigcheck_info *si = &sigtab[signum];

  struct sigcheck_info si_old = *si;

  if(handler == SIG_ERR || signum < 1 || signum >= _NSIG || handler == (void *)sigcheck)
    return signal_real(signum, handler);

  if (handler != SIG_IGN && handler != SIG_DFL) {
    if(verbose) {
      about_signal(signum);
      SAY("setting up a handler\n");
    }
    si->user_handler.h = handler;
    si->is_handled = 1;
    si->siginfo = 0;
    handler = (sighandler_t)sigcheck;
  } else {
    if(verbose) {
      about_signal(signum);
      SAY("clearing a handler\n");
    }
    si->is_handled = 0;
  }

  sighandler_t res = signal_real(signum, handler);
  if(res == SIG_ERR)
    *si = si_old;

  return res;
}

EXPORT int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  static int (*sigaction_real)(int signum, const struct sigaction *act, struct sigaction *oldact) = 0;
  if(!sigaction_real) {
    // Non-atomic but who cares?
    sigaction_real = dlsym(RTLD_NEXT, "sigaction");
  }

  if(!act)
    return sigaction_real(signum, act, oldact);

  volatile struct sigcheck_info *si = &sigtab[signum];
  struct sigcheck_info si_old = *si;

  struct sigaction myact;

  int siginfo = (act->sa_flags & SA_SIGINFO) != 0;
  void *handler = siginfo ? (void *)act->sa_sigaction : (void *)act->sa_handler;

  if(handler == SIG_ERR || signum < 1 || signum >= _NSIG || handler == sigcheck)
    return sigaction_real(signum, act, oldact);

  if (handler != SIG_IGN && handler != SIG_DFL) {
    if(verbose) {
      about_signal(signum);
      SAY("setting up a handler\n");
    }
    si->user_handler.h = handler;
    si->is_handled = 1;
    si->siginfo = siginfo;
    if(siginfo)
      si->user_handler.sa = act->sa_sigaction;
    else
      si->user_handler.h = act->sa_handler;
    myact.sa_sigaction = sigcheck;
    myact.sa_flags = act->sa_flags | SA_SIGINFO;
    myact.sa_mask = act->sa_mask;
    act = &myact;
  } else {
    if(verbose) {
      about_signal(signum);
      SAY("clearing a handler\n");
    }
    si->is_handled = 0;
  }

  int res = sigaction_real(signum, act, oldact);
  if(res != 0)
    *si = si_old;
  else if(fork_tests == FORK_TESTS_ONSET) {
    fork_test(signum);
  }

  return res;
}

DEFINE_ALIAS(EXPORT sighandler_t sysv_signal(int signum, sighandler_t handler), signal);
DEFINE_ALIAS(EXPORT sighandler_t bsd_signal(int signum, sighandler_t handler), signal);

