# SPDX-License-Identifier: GPL-2.0
#
# Static nolibc build of the m68k put_user() writeback fault reproducer.
#
#   make CROSS_COMPILE=.../m68k-linux-    # build for m68k
#   make ARCH=x86 CROSS_COMPILE=          # build natively, to check the test
#
# SYSROOT must contain the nolibc headers plus the uapi headers, as installed
# by "make -C tools/include/nolibc ARCH=$(ARCH) OUTPUT=... headers_standalone"
# in a kernel tree.

ARCH		?= m68k
CROSS_COMPILE	?= m68k-linux-
CC		 = $(CROSS_COMPILE)gcc
SYSROOT		?= sysroot/$(ARCH)/include

CFLAGS	= -Os -Wall -Wextra -fno-stack-protector -fno-ident \
	  -nostdlib -nostdinc -static \
	  -isystem $(SYSROOT) -include nolibc.h
LDFLAGS	= -static -nostdlib

all: sysbadaddr-$(ARCH)

sysbadaddr-$(ARCH): sysbadaddr.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lgcc

clean:
	rm -f sysbadaddr-*

.PHONY: all clean
