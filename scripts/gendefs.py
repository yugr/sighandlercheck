#!/usr/bin/python

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

def get_public_funs(lib):
  p = subprocess.Popen(
    ['readelf', '--dyn-syms', '-W', lib], 
    stdout = subprocess.PIPE
  )
  (stdout, stderr) = p.communicate()
  if p.returncode != 0:
    error("readelf failed: %s" % stderr)

  prefix = re.sub(r'^([a-zA-Z0-9_]+).*$', '\\1', os.path.basename(lib))

  res = []
  for line in stdout.split('\n'):
    # Don't consider undefined symbols
    if re.search(r'\bUND\b', line):
      continue

    # Skip internal functions
    if re.search(r'GLIBC_PRIVATE|^__libc_', line):
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

    res.append((name, addr, prefix))

  return res

# TODO: is there automated way to do this?
#libroot = "/lib/i386-linux-gnu/i686/cmov"
libroot = "/lib/x86_64-linux-gnu"

syms = []
for lib in [ "libc.so.6", "libm.so.6", "libpthread.so.0" ]:
#for lib in [ "libc.so.6", "libm.so.6"]:
  syms += get_public_funs(os.path.join(libroot, lib))

async_safe_syms = set(open(os.path.join(script_dir, 'async_safe_syms')).read().split('\n'))

syms = filter(lambda s: s not in async_safe_syms, syms)
syms.sort(key = lambda s: s[0])

syms_no_dups = []
for i in range(0, len(syms)):
  if not syms_no_dups or syms_no_dups[-1][0] != syms[i][0]:
    syms_no_dups.append(syms[i])

for (name, addr, lib) in syms_no_dups:
  print "SYMBOL(%s, 0x%x, %s)" % (name, addr, lib)
print "#undef SYMBOL"

