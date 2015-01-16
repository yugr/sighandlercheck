#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

void myhandler(int signum) {
  signum = signum;
  errno = 0;
}

int main() {
  if(SIG_ERR == signal(SIGHUP, myhandler)) {
    fprintf(stderr, "signal() failed\n");
    exit(1);
  }
  raise(SIGHUP);
  printf("Hello from main!\n");
  return 0;
}

