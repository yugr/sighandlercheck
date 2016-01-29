# What is this?

Signal Checker is a small proof-of-concept tool for detecting
unsafe signal handlers. C standard requires signal handlers
to only call reentrant subset of libc.

The tool works by preloading a DSO (libsigcheck.so) to a process.
The DSO intercepts all (!) libc functions. The interceptors check
whether they run in unsafe context.

The tool is rather hacky but it was able to detect potential errors
in popular programs (zip, aspell).

All credits for the idea (but not for it's ugly implementation)
should go to Michal Zalewski (aka [lcamtuf](http://lcamtuf.coredump.cx)).

The project is MIT licensed. It does not have any fancy dependencies,
just Glibc, GCC and Python.

# Why should I care about signal safety?

Check lcamtuf's [Delivering Signals for Fun and Profit]
(http://lcamtuf.coredump.cx/signals.txt) or CWE's
[Signal Handler with Functionality that is not Asynchronous-Safe]
(https://cwe.mitre.org/data/definitions/828.html).

Basically unsafe signal handlers may be used to exploit enclosing process.
This is particularly dangerous on Linux, where ordinary user can send signal
to setuid process.

# What are current results?

Quite interesting: I saw unsafe behavior in archivers (tar, bzip2, zip, etc.),
Texinfo, aspell, make, calendar, gpg and gdb (see scripts/examples for details).

# Usage

Run your app under sigcheck tool and send it a signal:

```
 $ sigcheck myapp ... > /dev/null &
 $ kill -HUP $!
```

Instead of manually sending the signals, you can ask the tool to automate it
by setting SIGCHECK\_FORK\_TESTS environment variable to "atexit"
(to send signals prior to exiting program) or to "onset"
(to send signal immediately after it got set). Both may find different sets of
bugs for different applications.

Other influential environment variables:
* SIGCHECK\_VERBOSE        - print debug info
* SIGCHECK\_MAX\_ERRORS     - limit number of reported errors
* SIGCHECK\_OUTPUT\_FILENO - output file descriptor (TODO: make this a filename?)

For some examples, see scripts/examples.

# Build

To build the tool, simply run ./build.sh from project top directory.
This has only been tested in Ubuntu 14.04.

# Test

To test the tool, run scripts/runtests.sh from project top directory.
Real-world examples are available in scripts/examples.

# Future plans

The main high-level items are
* run a complete distro under this (e.g. by putting libsigcheck.so to /etc/ld.so.preload)
* design (basically whether all this should be rewritten to use uprobes)
* interception of libc is ugly ugly (although efficient)
* make code thread-safe

Also various TODOs are scattered all over the codebase.

