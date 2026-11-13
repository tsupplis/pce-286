/* Refuse metadata writes after an overlay sector has reached its RAM base. */
#include <drivers/block/blkpbi.h>
#include <drivers/block/blkqed.h>
#include <drivers/block/blkram.h>
#include <drivers/block/blkpart.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <string.h>
#include <stdio.h>

static int check_format(const char *path, int qed)
{
	unsigned char sector[512], observed[512];
	struct rlimit saved, limit;
	int result;
	disk_t *base = dsk_ram_open(NULL, 128, 1, 1, 128, 0);
	if (!base) return 1;
	disk_t *overlay = qed ? dsk_qed_cow_create(base,path,128,4096)
	                      : dsk_pbi_cow_create(base,path,128,1,1,128,4096);
	if (!overlay) return 1;
	memset(sector, 0x59, sizeof sector);
	if (dsk_write_lba(overlay,sector,0,1)) return 1;
	if (getrlimit(RLIMIT_FSIZE,&saved)) return 1;
	limit = saved;
	limit.rlim_cur = 0;
	if (setrlimit(RLIMIT_FSIZE,&limit)) return 1;
	result = dsk_commit(overlay);
	if (setrlimit(RLIMIT_FSIZE,&saved)) return 1;
	if (!result || dsk_read_lba(base,observed,0,1) || memcmp(observed,sector,512)) return 1;
	if (!dsk_read_lba(overlay,observed,0,1) || !dsk_write_lba(overlay,sector,0,1)) return 1;
	dsk_del(overlay);
	/* Fresh zero base ensures that the persisted overlay mapping supplies the sector. */
	base = dsk_ram_open(NULL,128,1,1,128,0);
	if (!base) return 1;
	overlay = qed ? dsk_qed_cow_open(base,path) : dsk_pbi_cow_open(base,path);
	if (!overlay || dsk_read_lba(overlay,observed,0,1) || memcmp(observed,sector,512)) return 1;
	if (dsk_commit(overlay) || dsk_read_lba(base,observed,0,1) || memcmp(observed,sector,512)) return 1;
	dsk_del(overlay);
	return 0;
}

static int check_partition(void)
{
	struct rlimit saved, limit;
	unsigned char data[512] = {0x55}, seen[512];
	FILE *fp = tmpfile();
	disk_t *disk = dsk_part_open(1,1,1,0);
	if (!fp || !disk || dsk_part_add_partition_fp(disk,fp,1,0,0,1,0)) return 1;
	if (dsk_write_lba(disk,data,0,1) || getrlimit(RLIMIT_FSIZE,&saved)) return 1;
	limit = saved;
	limit.rlim_cur = 0;
	if (setrlimit(RLIMIT_FSIZE,&limit)) return 1;
	data[0] = 0x77;
	int result = dsk_write_lba(disk,data,0,1);
	if (setrlimit(RLIMIT_FSIZE,&saved)) return 1;
	data[0] = 0x55;
	int failed = !result || pread(fileno(fp),seen,sizeof seen,0) != sizeof seen || memcmp(data,seen,sizeof data);
	for (unsigned i = 1; i < DSK_PART_MAX; i++) {
		if (dsk_part_add_partition_fp(disk,fp,0,0,i,1,0)) failed = 1;
	}
	if (!dsk_part_add_partition_fp(disk,fp,0,0,DSK_PART_MAX,1,0)) failed = 1;
	dsk_del(disk);
	return failed;
}

int main(int argc, char **argv)
{
	if (argc != 3 || signal(SIGXFSZ,SIG_IGN)==SIG_ERR) return 1;
	int pbi = check_format(argv[1],0);
	int qed = check_format(argv[2],1);
	printf("Metadata refusal, reopen, retry: PBI %s, QED %s\n",pbi?"FAIL":"PASS",qed?"FAIL":"PASS");
	int partition = check_partition();
	printf("Partition write refusal: %s\n", partition?"FAIL":"PASS");
	return pbi || qed || partition;
}
