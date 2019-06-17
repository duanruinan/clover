#ifndef CLOVER_REGION_H
#define CLOVER_REGION_H

#include <clover_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

struct clv_region_data {
	u32 size;
	u32 count_boxes;
	struct clv_box boxes[0];
};

struct clv_region {
	struct clv_box extents;
	struct clv_region_data *data;
};

void clv_region_init(struct clv_region *region);

void clv_region_init_rect(struct clv_region *region, s32 x, s32 y,u32 w, u32 h);

s32 clv_region_init_boxes(struct clv_region *region,
			  const struct clv_box *boxes, s32 count);

void clv_region_init_with_extents(struct clv_region *region,
				  struct clv_box *extents);

void clv_region_fini(struct clv_region *region);

void clv_region_translate(struct clv_region *region, s32 x, s32 y);

s32 clv_region_copy(struct clv_region *dst, struct clv_region *src);

s32 clv_region_intersect(struct clv_region *n, struct clv_region *r1,
			 struct clv_region *r2);

s32 clv_region_intersect_rect(struct clv_region *dst, struct clv_region *src,
			      s32 x, s32 y, u32 w, u32 h);

s32 clv_region_union(struct clv_region *n, struct clv_region *r1,
		     struct clv_region *r2);

s32 clv_region_union_rect(struct clv_region *dst, struct clv_region *src,
			  s32 x, s32 y, u32 w, u32 h);

s32 clv_region_subtract(struct clv_region *n, struct clv_region *r1,
			struct clv_region *r2);

s32 clv_region_count_boxes(struct clv_region *region);

struct clv_box * clv_region_extents(struct clv_region *region);

s32 clv_region_is_not_empty(struct clv_region *region);

struct clv_box * clv_region_boxes(struct clv_region *region, s32 *count_boxes);

void clv_region_clear(struct clv_region *region);

#ifdef __cplusplus
}
#endif

#endif

