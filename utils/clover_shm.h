#ifndef CLOVER_SHM_H
#define CLOVER_SHM_H

#include <clover_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLV_SHM_NM_MAX_LEN 128

struct clv_shm {
	char name[CLV_SHM_NM_MAX_LEN];
	s32 fd;
	void *map;
	u32 sz;
	s32 creator;
};

s32 clv_shm_init(struct clv_shm *shm, const char *shm_id, u32 size,
		 s32 creator);
void clv_shm_release(struct clv_shm *shm);

#ifdef __cplusplus
}
#endif

#endif

