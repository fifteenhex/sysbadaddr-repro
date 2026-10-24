# Findings: testing the put_user() writeback fix on real hardware

This records what the reproducer in this repo shows on **physical** 680x0
machines, which is the evidence the lore thread
([m68k: Handle put_user() faults more carefully](https://lore.kernel.org/linux-m68k/1ed9c4c753395510c1a8df9c35e2ad4c31c90f95.1714265926.git.fthain@linux-m68k.org/))
was missing: QEMU does not emulate the 68040 partial-writeback bus-error
frame, so a clean run under emulation proves nothing.

## Setup

- Reproducer: `sysbadaddr-m68k` from this repo (static 68020 nolibc binary).
  It runs the `bad_end_addr` case three times, at 1, 2 and 3 bytes back from
  the end of a mapped r/w page, so the first `moves.l` store of
  `__generic_copy_to_user()` straddles the page boundary into a hole.
- Kernel: mainline Linux 7.3.0.
- Hardware: a real **MC68040** system and a real **MC68030** (MVME147) system.
  Both are physical machines, not emulation.
- The patch under test: Finn Thain's `arch/m68k/include/asm/uaccess.h` change
  (a copy is in `finn-putuser.patch` here) — a `nop` after the `moves` in the
  `put_user()` asm so the exception-frame PC is dependable. It is **not**
  upstream as of 7.3.0; the current tree's last change to this code was the
  `set_fs()` removal.

## Results

The reproducer prints one line per offset before the syscall it describes;
"ok" means the syscall returned `EFAULT` as it should, an oops means the
kernel faulted instead.

| CPU | Patch | offset 1 (…fff) | offset 2 (…ffe) | offset 3 (…ffd) |
|-----|-------|-----------------|-----------------|-----------------|
| MC68040 | none  | **oops**        | —               | —               |
| MC68040 | Finn's | EFAULT ok       | EFAULT ok       | **oops**        |
| MC68030 | none  | **oops**        | —               | —               |
| MC68030 | Finn's | **oops**        | —               | —               |

Raw (de-duplicated, login-noise-stripped) oops dumps are the `*.log` files
alongside this one.

**Conclusion: the patch is incomplete on this kernel.** It genuinely works as
far as it goes — on the 68040 it converts the offset-1 and offset-2 oopses
into clean `EFAULT` — but the offset-3 case on the 68040 still oopses, and on
the 68030 the patch has no effect at all.

## Why

The straddling store faults inside `__generic_copy_to_user()`'s own `moves.l`
loop (`arch/m68k/lib/uaccess.c`). That store already has an exception-table
entry, but the two CPUs recover differently and the patch only touches one of
the two paths:

- **68040** — the access-error is deferred to the writeback mechanism:
  `access_error040()` → `do_040writebacks()` → `do_040writeback1()`, which
  re-issues the straddling store with **`put_user()`**. That is the path
  Finn's patch fixes, so offsets 1 and 2 recover. Offset 3 escapes it: the
  faulting instruction moves on to the byte-store tail of the copy routine
  (`__generic_copy_to_user+0x36`, marked `<1418>` in the `Code:` line, versus
  `+0x22`/`<66f6>` for offset 1), a case the single `put_user` `nop` does not
  cover.
- **68030** — the fault is a format-B bus error handled by `bus_error030()` →
  `do_page_fault()` → `send_fault_sig()`. **No `put_user()` is involved**, so
  a `put_user()`-only patch is a complete no-op here; the 030 oopses with and
  without it.

So the root cause is better framed as the copy routine's `moves` store
reporting a PC that the exception table cannot match (per Michael Schmitz's
analysis on the thread: on a writeback fault the frame PC is where the fault
was *detected*, not the faulting `moves`). Finn's fix applied the "flush the
pipeline with a `nop` so the PC is dependable" idea to `put_user()`; a
complete fix appears to need the same treatment on the copy/clear routines'
own `moves` stores (which is the path both CPUs share, and the only path the
030 uses), and possibly additional care for the 68040's second writeback.

## Note on the thread's "tested on 68030 and 68040"

The posted patch carries `Reviewed-and-tested-by` and says it was tested on
68030 and 68040. That does not match what is seen here, most likely because
the surrounding `copy_to_user`/bus-error code has moved on between the
patch's kernel (~2024) and 7.3.0, leaving new cases (68040 offset 3, and the
whole 68030 path) uncovered. These dumps are offered as current real-hardware
data, not as a refutation of the original testing.
