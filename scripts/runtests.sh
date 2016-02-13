#!/bin/sh

# Copyright 2015-2016 Yury Gribov
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

cd $(dirname $0)/..

SIGCHECK=bin/sigcheck
CFLAGS='-g -O2 -Wall -Wextra -Werror'

status=0
for f in tests/*.c; do
  gcc $f -o bin/test.out $CFLAGS
  $SIGCHECK bin/test.out > bin/test.log 2>&1
  sed -i -e 's!(pid [0-9]\+)!(pid PID)!g' bin/test.log
  if ! diff $f.log bin/test.log; then
    echo "$f: FAIL"
    status=1
  else
    echo "$f: SUCCESS"
  fi
done

return $status

