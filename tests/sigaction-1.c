#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

void myhandler(int signum) {
  printf("Hello from myhandler: %d\n", signum);
}

int main() {
  struct sigaction sa;
  sa.sa_handler = myhandler;
  sa.sa_flags = 0;
  sigemptyset (&sa.sa_mask);
  if(0 != sigaction(SIGHUP, &sa, 0)) {
    fprintf(stderr, "sigaction() failed\n");
    exit(1);
  }
  raise(SIGHUP);
  return 0;
}

