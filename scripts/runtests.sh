#!/bin/sh

cd $(dirname $0)/..

SIGCHECK=$PWD/bin/libsigcheck.so
CFLAGS='-g -O2 -Wall -Wextra -Werror'

status=0
for f in tests/*.c; do
  gcc $f -o bin/test.out $CFLAGS
  LD_PRELOAD=$SIGCHECK bin/test.out > bin/test.log 2>&1
  sed -i -e 's!(pid [0-9]\+)!(pid PID)!g' bin/test.log
  if ! diff $f.log bin/test.log; then
    echo "$f: FAIL"
    status=1
  else
    echo "$f: SUCCESS"
  fi
done

return $status

