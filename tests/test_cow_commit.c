/* Terminate the committer between two nonadjacent dirty sectors. */
#include <drivers/block/blkraw.h>
#include <drivers/block/blkcow.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>

static dsk_write_f base_write;
static int interrupt_write(disk_t *disk, const void *buf, uint32_t i, uint32_t n)
{
	if (i == 2) _exit(42);
	return base_write(disk, buf, i, n);
}

int main(int argc, char **argv)
{
	unsigned char a[512], b[512], buf[1536], expected[1536] = {0};
	disk_t *base, *overlay;
	int status;
	pid_t child;

	if (argc != 3 || dsk_img_create(argv[1], 3, 0)) return 1;
	memset(a, 0x35, sizeof a);
	memset(b, 0x79, sizeof b);
	memcpy(expected, a, 512);
	memcpy(expected + 1024, b, 512);
	base = dsk_img_open(argv[1], 0, 0);
	if (!base || !(overlay = dsk_cow_new(base, argv[2]))) return 1;
	if (dsk_write_lba(overlay, a, 0, 1) || dsk_write_lba(overlay, b, 2, 1)) return 1;
	dsk_del(overlay);
	child = fork();
	if (child == 0) {
		base = dsk_img_open(argv[1], 0, 0);
		if (!base || !(overlay = dsk_cow_new(base, argv[2]))) _exit(1);
		base_write = base->write;
		base->write = interrupt_write;
		dsk_commit(overlay);
		_exit(1);
	}
	if (child < 0 || waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status) != 42) return 1;
	base = dsk_img_open(argv[1], 0, 0);
	if (!base || dsk_read_lba(base, buf, 0, 3) ||
	    memcmp(buf, expected, 1024) || buf[1024] != 0) return 1;
	overlay = dsk_cow_new(base, argv[2]);
	if (!overlay || dsk_read_lba(overlay, buf, 0, 3) || memcmp(buf, expected, sizeof buf)) {
		fprintf(stderr, "FAIL reopened overlay after interrupted commit\n");
		return 1;
	}
	if (dsk_commit(overlay)) return 1;
	dsk_del(overlay);
	base = dsk_img_open(argv[1], 0, 0);
	if (!base || dsk_read_lba(base, buf, 0, 3) || memcmp(buf, expected, sizeof buf)) return 1;
	dsk_del(base);
	puts("PASS interrupted COW commit, reopen and retry");
	return 0;
}
