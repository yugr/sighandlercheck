#!/usr/bin/python

# Copyright 2015-2020 Yury Gribov
# 
# Use of this source code is governed by MIT license that can be
# found in the LICENSE.txt file.

# TODO:
# * support versioned symbols?

import os
import os.path
import subprocess
import sys
import re

script_dir = os.path.dirname(sys.argv[0])   

def error(msg):
  sys.stderr.write('error: %s\n' % msg)
  sys.exit(1)

def safe_run(cmd):
  p = subprocess.Popen(cmd, stdout = subprocess.PIPE)
  res = p.communicate()
  if p.returncode != 0:
    _, stderr = res
    error("%s failed: %s" % (cmd[0], stderr))
  return res

def find_glibc():
  stdout, stderr = safe_run(['ldd', '/bin/sh'])
  libc_name = 'libc.so.6'
  for line in stdout.split('\n'):
    idx = line.find(libc_name + ' => ')
    if idx == -1:
      continue
    # libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007fab5a92f000)
    lib = line[idx:].split(' ')[2]
    root = os.path.dirname(lib)
    link = os.readlink(lib)
    if not link:
      error('failed to readlink %s' % link)
    ver = re.findall(r'[0-9]+\.[0-9]+', link)[0]
    return (root, ver)
  error('failed to locate ' + libc_name)

def get_public_funs(lib, filename):
  stdout, stderr = safe_run(['readelf', '--dyn-syms', '-W', filename])

  res = []
  for line in stdout.split('\n'):
    # Don't consider undefined symbols
    if re.search(r'\bUND\b', line):
      continue

    # Skip internal functions
    # TODO: __libc_memalign is _not_ private!
    if re.search(r'GLIBC_PRIVATE|^__libc_|__errno_location', line):
      continue

    # Only consider default version
    # TODO: take care of legacy versions
    if not re.search(r'@@GLIBC_', line):
      continue

    # Only consider functions
    if not re.search(r'\bFUNC\b', line):
      continue

    #print line
    words = re.split(r'\s+', line)
    name = re.sub(r'@.*', '', words[8])
    addr = int("0x" + words[2], 16)

    # We intercept signal API separately (and it's async-safe anyway)
    if re.match(r'^(signal|sysv_signal|bsd_signal|sigaction)$', name):
      continue

    # We need these during initialization and error reporting.
    # Hopefully they don't call other libc functions...
    if re.match(r'^(open|close|read|write)$', name):
      continue

    res.append((name, addr, lib))

  return res

if len(sys.argv) != 2:
  error('invalid syntax')
out = sys.argv[1]

libroot, libver = find_glibc()

# TODO: other parts of glibc: crypt, resolv, dl, rt, nss*, nsl, etc.)?
# See http://www.faqs.org/docs/linux_scratch/appendixa/glibc.html
libs = map(
  lambda lib: (lib, '%s-%s.so' % (lib, libver)),
  ['libc', 'libm', 'libpthread']
)

syms = []
for lib, filename in libs:
  syms += get_public_funs(lib, os.path.join(libroot, filename))

async_safe_syms = open(os.path.join(script_dir, 'async_safe_syms')).read().split('\n')
async_safe_syms = filter(lambda s: s, async_safe_syms)
async_safe_syms = filter(lambda s: s[0] != '#', async_safe_syms)
async_safe_syms = set(async_safe_syms)

syms = filter(lambda s: s[0] not in async_safe_syms, syms)
syms.sort(key = lambda s: s[0])

syms_no_dups = []
for i in range(0, len(syms)):
  if not syms_no_dups or syms_no_dups[-1][0] != syms[i][0]:
    syms_no_dups.append(syms[i])

f = open(os.path.join(out, 'all_libc_syms.def'), 'w')
f.write('''
#ifndef SYMBOL
#error You must define SYMBOL(name, addr, lib)
#endif
''')
for name, addr, lib in syms_no_dups:
  f.write("SYMBOL(%s, 0x%x, %s)\n" % (name, addr, lib))
f.write("#undef SYMBOL\n")
f.close()

f = open(os.path.join(out, 'all_libc_libs.def'), 'w')
f.write('''
#ifndef LIBRARY
#error You must define LIBRARY(name, filename)
#endif
''')
for name, filename in libs:
  f.write('LIBRARY(%s, "%s")\n' % (name, filename))
f.write('#undef LIBRARY\n')
f.close()

