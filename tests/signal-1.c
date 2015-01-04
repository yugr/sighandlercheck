#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

void myhandler(int signum) {
  printf("Hello from myhandler: %d\n", signum);
}

int main() {
  if(SIG_ERR == signal(SIGHUP, myhandler)) {
    fprintf(stderr, "signal() failed\n");
    exit(1);
  }
  raise(SIGHUP);
  return 0;
}

