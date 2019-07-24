#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_event.h>
#include <clover_shm.h>

s32 clv_shm_init(struct clv_shm *shm, const char *shm_id, u32 size, s32 creator)
{
	memset(shm, 0, sizeof(*shm));
	strcpy(shm->name, shm_id);
	shm->sz = size;

	printf("shm->name = %s size = %u creator = %d %s\n", shm->name, size,
		creator, strerror(errno));
	shm->creator = creator;
	if (shm->creator)
		shm->fd = shm_open(shm->name,
				   O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	else
		shm->fd = shm_open(shm->name, O_RDWR, S_IRUSR | S_IWUSR);
	if (shm->fd < 0) {
		clv_err("shm_open %s fail. %m", shm_id);
		return -errno;
	}

	if (clv_set_cloexec_or_close(shm->fd) < 0)
		clv_warn("failed to set shm fd cloexec.");

	printf("fd = %d, %s\n", shm->fd, strerror(errno));
	ftruncate(shm->fd, shm->sz);
	shm->map = mmap(NULL, shm->sz, PROT_READ | PROT_WRITE,
			MAP_SHARED, shm->fd, 0);
	printf("sz = %u errno = %s\n", shm->sz, strerror(errno));

	if (!shm->map)
		return -errno;

	return 0;
}

void clv_shm_release(struct clv_shm *shm)
{
	if (shm->map)
		munmap(shm->map, shm->sz);

	shm->map = NULL;

	if (shm->fd)
		close(shm->fd);

	shm->fd = 0;
	shm->sz = 0;

	if (shm->creator)
		shm_unlink(shm->name);

	shm->creator = 0;

	memset(shm->name, 0, CLV_SHM_NM_MAX_LEN);
}

