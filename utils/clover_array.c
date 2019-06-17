#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <clover_utils.h>
#include <clover_array.h>

void clv_array_init(struct clv_array *array)
{
	memset(array, 0, sizeof(*array));
}

void clv_array_release(struct clv_array *array)
{
	if (array->data)
		free(array->data);
	array->data = NULL;
}

void * clv_array_add(struct clv_array *array, u32 size)
{
	u32 alloc;
	void *data, *p;

	if (!array || !size)
		return NULL;

	if (array->alloc > 0)
		alloc = array->alloc;
	else
		alloc = 16;

	while (alloc < array->size + size)
		alloc *= 2;

	if (array->alloc < alloc) {
		if (array->alloc > 0)
			data = realloc(array->data, alloc);
		else
			data = malloc(alloc);

		if (data == NULL)
			return NULL;
		array->data = data;
		array->alloc = alloc;
	}

	p = array->data + array->size;
	array->size += size;

	return p;
}

s32 clv_array_copy(struct clv_array *dst, struct clv_array *src)
{
	if (!dst || !src)
		return -EINVAL;

	if (dst->size < src->size) {
		if (!clv_array_add(dst, src->size - dst->size))
			return -1;
	} else {
		dst->size = src->size;
	}

	memcpy(dst->data, src->data, src->size);
	return 0;
}

