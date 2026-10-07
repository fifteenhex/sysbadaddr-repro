# m68k put_user() writeback fault reproducer

A ~4.5K static binary, no libc, that reproduces the case behind
[m68k: Handle put_user() faults more carefully](https://lore.kernel.org/linux-m68k/1ed9c4c753395510c1a8df9c35e2ad4c31c90f95.1714265926.git.fthain@linux-m68k.org/)
without needing a toolchain or a stress-ng build on the machine under test.

It is the `bad_end_addr` case of `stress-ng --sysbadaddr` and nothing else:
`getcwd()` is handed a buffer whose first byte is the last byte of a mapped
r/w anonymous page and whose remainder falls in a hole.
`__generic_copy_to_user()` copies the path with a `moves.l` loop and no
alignment fixup, so the first store straddles the page boundary. On a 68040
the first half is written back and the second half faults, and the fault is
taken in `do_040writeback1()` -> `put_user()` instead of in the original
`moves.l`.

Three offsets are tried: 1, 2 and 3 bytes back from the end of the page. All
three put the first longword store across the boundary. 4 bytes back is not
interesting, the store lands inside the page and the next one faults cleanly.

## Build

Needs a nolibc sysroot, which is the nolibc headers plus the uapi headers:

    make -C tools/include/nolibc ARCH=m68k OUTPUT=/path/to/here/ \
        headers_standalone

Then, with a kernel.org m68k nolibc toolchain:

    make CROSS_COMPILE=.../m68k-linux-

The result is a static 68020 binary, so it runs on 020 through 060. Build
natively with `make ARCH=x86 CROSS_COMPILE=` to check the test itself on a
machine that is not affected.

## Run

    ./sysbadaddr-m68k [rounds]

The working directory has to be at least four bytes long, otherwise
`copy_to_user()` has no full longword to store and never issues the
straddling `moves.l`. The binary takes care of this by `chdir()`ing to
`/tmp` or similar if the inherited cwd is too short, and prints what it
settled on.

Each line is printed before the syscall it describes, so a serial log shows
which offset the machine died on.

## Expected output

    m68k put_user() writeback fault reproducer
    page size 4096, 1 round
    cwd is "/tmp" (5 bytes incl. NUL)
      getcwd(0xc0000fff, 4096): last 1 byte of the page ... EFAULT, ok
      getcwd(0xc0000ffe, 4096): last 2 bytes of the page ... EFAULT, ok
      getcwd(0xc0000ffd, 4096): last 3 bytes of the page ... EFAULT, ok
    survived, 0 unexpected results

The bug is an "Unable to handle kernel access at virtual address ..." oops in
`do_040writeback1()` in place of one of those EFAULT lines.

## What has and has not been checked

The output above is from a 68040 kernel under QEMU (`-M virt`), which is
enough to show that the test reaches the straddling copy and gets the
EFAULT it should. It is not evidence either way about the bug: QEMU does not
emulate the 68040 partial-writeback bus error frame that the oops comes from,
so a clean run there means nothing. Real hardware is needed, per Finn's reply
on the thread.

Verified natively on x86-64 as well, to confirm the straddling buffer is
accepted by `access_ok()` and the copy is what faults.
