#ifndef CLOVER_ARRAY_H
#define CLOVER_ARRAY_H

#include <clover_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

struct clv_array {
	u32 size;
	u32 alloc;
	void *data;
};

void clv_array_init(struct clv_array *array);
void clv_array_release(struct clv_array *array);
void *clv_array_add(struct clv_array *array, u32 size);
s32 clv_array_copy(struct clv_array *dst, struct clv_array *src);

#define clv_array_for_each_entry(p, array) \
	for (p = (array)->data; \
	    (const char *)p < ((const char *)(array)->data + (array)->size); \
	    (p)++)

#ifdef __cplusplus
}
#endif

#endif

