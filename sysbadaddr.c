/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal reproducer for the m68k put_user() writeback fault oops reported in
 * "[PATCH] m68k: Handle put_user() faults more carefully":
 * https://lore.kernel.org/linux-m68k/1ed9c4c753395510c1a8df9c35e2ad4c31c90f95.1714265926.git.fthain@linux-m68k.org/
 *
 * This is the 'bad_end_addr' case of 'stress-ng --sysbadaddr', reduced to one
 * syscall so that it can be built with nolibc and dropped onto a machine
 * without a toolchain or a stress-ng build.
 *
 * getcwd() is handed a buffer which starts on the last byte of a mapped r/w
 * anonymous page and runs into a hole. __generic_copy_to_user() copies the
 * path with a "moves.l" loop and no alignment fixup, so the first store
 * straddles the page boundary. On a 68040 the first half is written back and
 * the second half faults, and the fault is taken in do_040writeback1() ->
 * put_user() rather than in the original moves.l.
 *
 * Expected result on every offset: EFAULT.
 * The bug looks like an "Unable to handle kernel access" oops in
 * do_040writeback1() instead.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * nolibc gained getcwd() late and renamed its syscall macros along the way,
 * so go straight to the syscall. The raw return value is what is wanted
 * here anyway: the errno the kernel produced rather than -1 and a global.
 */
#if defined(__nolibc_syscall2)
#define repro_syscall2 __nolibc_syscall2
#elif defined(my_syscall2)
#define repro_syscall2 my_syscall2
#else
#error "no nolibc syscall macro found"
#endif

static int sys_getcwd(char *buf, unsigned long size)
{
	return repro_syscall2(__NR_getcwd, buf, size);
}

/*
 * A path of at least four bytes is needed, otherwise copy_to_user() has no
 * full longword to store and never issues the straddling moves.l.
 */
#define MIN_CWD_LEN	4

static const char *const cwd_candidates[] = {
	"/tmp", "/var/tmp", "/usr", "/etc", "/home", NULL
};

static unsigned long page_size(void)
{
	unsigned long sz = getauxval(AT_PAGESZ);

	return sz ? sz : 4096;
}

/*
 * Pick a working directory whose path is long enough to be copied with at
 * least one longword store. Returns the length getcwd() reports, including
 * the terminating NUL.
 */
static int setup_cwd(void)
{
	char buf[256];
	int len, i;

	len = sys_getcwd(buf, sizeof(buf));
	if (len >= MIN_CWD_LEN) {
		printf("cwd is \"%s\" (%d bytes incl. NUL)\n", buf, len);
		return len;
	}

	for (i = 0; cwd_candidates[i]; i++) {
		if (chdir(cwd_candidates[i]))
			continue;
		len = sys_getcwd(buf, sizeof(buf));
		if (len < MIN_CWD_LEN)
			continue;
		printf("cwd is \"%s\" (%d bytes incl. NUL)\n", buf, len);
		return len;
	}

	printf("no usable cwd found, cd somewhere at least %d bytes deep\n",
	       MIN_CWD_LEN);
	return -1;
}

/*
 * Map a r/w anonymous page which is guaranteed to be followed by a hole, and
 * fault it in so that only the access past its end can fault.
 */
static char *map_page_before_hole(unsigned long pgsz)
{
	char *p;

	p = mmap(NULL, 2 * pgsz, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		printf("mmap failed: %d\n", errno);
		return NULL;
	}

	if (munmap(p + pgsz, pgsz)) {
		printf("munmap failed: %d\n", errno);
		munmap(p, 2 * pgsz);
		return NULL;
	}

	memset(p, 0, pgsz);
	return p;
}

/*
 * Offsets measured back from the end of the page. 1, 2 and 3 all put the
 * first longword store across the boundary; 4 would land inside the page and
 * fault cleanly on the following store, which is the case that already works.
 */
static const unsigned long tail_offsets[] = { 1, 2, 3 };

static int run_one(unsigned long pgsz, unsigned long tail)
{
	char *p, *buf;
	int ret;

	p = map_page_before_hole(pgsz);
	if (!p)
		return -1;

	buf = p + pgsz - tail;

	/* Printed before the syscall so it survives in a serial log. */
	printf("  getcwd(%p, %lu): last %lu byte%s of the page ... ",
	       buf, pgsz, tail, tail == 1 ? "" : "s");

	ret = sys_getcwd(buf, pgsz);

	if (ret == -EFAULT)
		printf("EFAULT, ok\n");
	else if (ret < 0)
		printf("errno %d, unexpected\n", -ret);
	else
		printf("wrote %d bytes into a hole, unexpected\n", ret);

	munmap(p, pgsz);
	return ret;
}

int main(int argc, char **argv)
{
	unsigned long pgsz = page_size();
	int rounds = 1;
	int i, r, bad = 0;

	if (argc > 1)
		rounds = atoi(argv[1]);
	if (rounds < 1)
		rounds = 1;

	printf("m68k put_user() writeback fault reproducer\n");
	printf("page size %lu, %d round%s\n",
	       pgsz, rounds, rounds == 1 ? "" : "s");

	if (setup_cwd() < 0)
		return 1;

	for (r = 0; r < rounds; r++) {
		if (rounds > 1)
			printf("round %d:\n", r + 1);
		for (i = 0; i < (int)(sizeof(tail_offsets) /
				      sizeof(tail_offsets[0])); i++) {
			int ret = run_one(pgsz, tail_offsets[i]);

			if (ret != -EFAULT)
				bad++;
		}
	}

	printf("survived, %d unexpected result%s\n",
	       bad, bad == 1 ? "" : "s");
	return bad ? 1 : 0;
}
