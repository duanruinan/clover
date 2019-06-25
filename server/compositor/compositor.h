#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <clover_utils.h>
#include <clover_region.h>
#include <clover_shm.h>
#include <clover_signal.h>

struct clv_renderer;
struct clv_surface;
struct clv_view;

struct clv_compositor {
	struct clv_renderer *renderer;
	struct list_head views;
	struct list_head outputs;
};

struct clv_output {
	struct clv_compositor *c;
	void *renderer_state;
	struct clv_rect render_area; /* in canvas coordinates */
	struct list_head link;
};

struct clv_surface {
	struct clv_compositor *c;
	void *renderer_state;
	s32 is_opaque;
	struct clv_signal destroy_signal;
	struct clv_view *view;
	struct clv_region damage; /* used for texture upload */
	u32 w, h; /* surface size */
	struct clv_region opaque; /* opaque area */
};

enum clv_view_type {
	CLV_VIEW_TYPE_PRIMARY,
	CLV_VIEW_TYPE_OVERLAY,
	CLV_VIEW_TYPE_CURSOR,
};

struct clv_view {
	enum clv_view_type type;
	struct clv_surface *surface;
	struct list_head link;
	struct clv_rect area; /* in canvas coordinates */
	float alpha;
};

enum clv_buffer_type {
	CLV_BUF_TYPE_UNKNOWN = 0,
	CLV_BUF_TYPE_SHM,
	CLV_BUF_TYPE_DMA,
};

enum clv_pixel_fmt {
	CLV_PIXEL_FMT_UNKNOWN = 0,
	CLV_PIXEL_FMT_XRGB8888,
	CLV_PIXEL_FMT_ARGB8888,
	CLV_PIXEL_FMT_YUV420P,
	CLV_PIXEL_FMT_YUV444P,
};

#define CLV_BUFFER_NAME_LEN 128
struct clv_buffer {
	enum clv_buffer_type type;
	u32 w, h, stride;
	u32 size;
	enum clv_pixel_fmt pixel_fmt;
	s32 count_planes;
	char name[CLV_BUFFER_NAME_LEN];
	s32 fd;
};

struct shm_buffer {
	struct clv_buffer base;
	struct clv_shm shm;
};

struct clv_renderer {
	void (*repaint_output)(struct clv_output *output);
	void (*flush_damage)(struct clv_surface *surface);
	void (*attach_buffer)(struct clv_surface *surface,
			      struct clv_buffer *buffer);
	void (*destroy)(struct clv_compositor *c);
	s32 (*output_create)(struct clv_output *output,
			     void *window_for_legacy,
			     void *window,
			     s32 *formats,
			     s32 count_fmts,
			     s32 *vid);
	struct clv_buffer * (*import_dmabuf)(struct clv_compositor *c,
					     s32 dmabuf_fd,
					     u32 w,
					     u32 h,
					     u32 stride,
					     enum clv_pixel_fmt pixel_fmt,
					     u32 internal_fmt);
	void (*output_destroy)(struct clv_output *output);
};

s32 renderer_create(struct clv_compositor *c, s32 *formats, s32 count_fmts,
		    s32 no_winsys, void *native_window, s32 *vid);

void set_renderer_dbg(u8 flag);

#endif

