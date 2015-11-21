#!/bin/sh

# TODO:
# * support versioned symbols
# * compile with -ffreestanding to avoid autogenerated memset and crap

set -e

cd $(dirname $0)

OBJ=bin

mkdir -p $OBJ
rm -f $OBJ/*

scripts/gendefs.py $OBJ/

CPPFLAGS='-Iinclude -Ibin -D_GNU_SOURCE'

CFLAGS='-g -O2'
CFLAGS="$CFLAGS -Wall -Wextra -Werror"
CFLAGS="$CFLAGS -fvisibility=hidden -fPIC -shared"
#CFLAGS="$CFLAGS -g -O0 -save-temps"

LDFLAGS='-Wl,--no-as-needed -ldl -lm -lpthread'

gcc -o $OBJ/libsigcheck.so src/*.c $CPPFLAGS $CFLAGS $LDFLAGS

cp scripts/sigcheck $OBJ

echo 'Quick functionality check'
SIGCHECK_VERBOSE=1 $OBJ/sigcheck bash -c 'whoami; whoami'

