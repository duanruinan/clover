#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <time.h>
#include <assert.h>
#include <clover_utils.h>
#include <clover_region.h>
#include <clover_shm.h>
#include <clover_signal.h>

struct clv_renderer;
struct clv_surface;
struct clv_view;
struct clv_backend;
struct clv_compositor_config;
struct clv_output;
struct clv_head;
struct clv_plane;
struct clv_server;

struct clv_server {
	struct clv_event_loop *loop;
	struct clv_compositor *c;
	s32 exit;
	struct list_head clients;
};

struct clv_event_loop *clv_server_get_event_loop(struct clv_server *server);
struct clv_server *clv_server_create(void);
void clv_server_run(struct clv_server *server);
void clv_server_stop(struct clv_server *server);
void clv_server_destroy(struct clv_server *server);

struct clv_plane {
	struct clv_compositor *c;
	struct list_head link;
	/* struct clv_region damage; */
};

struct clv_compositor {
	struct clv_server *server;
	struct clv_backend *backend;
	struct clv_renderer *renderer;
	struct clv_plane root_plane;
	struct list_head views;
	struct list_head outputs;
	struct list_head pending_outputs;
	struct list_head heads;
	struct list_head planes;
	clockid_t presentation_clock;
};

enum clv_desktop_mode {
	CLV_DESKTOP_MODE_UNKNOWN = 0,
	CLV_DESKTOP_MODE_DUPLICATED,
	CLV_DESKTOP_MODE_EXTENDED,
};

#define DEV_NODE_LEN 128

struct clv_compositor_config {
	char dev_node[DEV_NODE_LEN];
	enum clv_desktop_mode mode;
	struct clv_region canvas;
};

struct clv_backend {
	void (*destroy)(struct clv_compositor *c);
	void * (*repaint_begin)(struct clv_compositor *c);
	void (*repaint_cancel)(struct clv_compositor *c, void *repaint_data);
	void (*repaint_flush)(struct clv_compositor *c, void *repaint_data);
	struct clv_output * (*output_create)(struct clv_compositor *c,
					     u32 head_index);
	void (*update_config)(struct clv_compositor *c,
			      struct clv_compositor_config *config);
};

void set_backend_dbg(u8 flag);

enum clv_output_mode {
	CLV_OUTPUT_MODE_CURRENT = 0x01,
	CLV_OUTPUT_MODE_PREFERRED = 0x02,
};

struct clv_mode {
	u32 flags;
	u32 w, h;
	u32 refresh;
	struct list_head link;
};

enum clv_dpms {
	CLV_DPMS_ON,
	CLV_DPMS_STANDBY,
	CLV_DPMS_SUSPEND,
	CLV_DPMS_OFF,
};

struct clv_output {
	struct clv_compositor *c;

	u32 index;

	void *renderer_state;

	struct clv_rect render_area; /* in canvas coordinates */

	s32 repaint_needed; /* true if damage occured since the last repaint */

	s32 repainted; /* used between repaint_begin and repaint_cancel */

	enum {
		REPAINT_NOT_SCHEDULED = 0,
		REPAINT_BEGIN_FROM_IDLE,
		REPAINT_SCHEDULED,
		REPAINT_AWAITING_COMPLETION,
	} repaint_status;

	/* if repaint status is REPAINT_SCHEDULED, contains the time the next
	 * repaint should be run */
	struct timespec next_repaint;

	struct clv_event_source *idle_repaint_source;

	struct clv_signal frame_signal;
	struct clv_signal destroy_signal; /* sent when disabled */

	struct timespec frame_time; /* presentation timestamp */
	u64 msc; /* media stream counter */

	struct clv_mode *current_mode;
	struct list_head modes;

	struct clv_head *head;

	s32 enabled;

	void (*start_repaint_loop)(struct clv_output *output);
	s32 (*repaint)(struct clv_output *output, void *repaint_data);
	void (*assign_planes)(struct clv_output *output, void *repaint_data);
	void (*destroy)(struct clv_output *output);
	s32 (*switch_mode)(struct clv_output *output, struct clv_mode *mode);
	void (*set_dpms)(struct clv_output *output, enum clv_dpms level);
	s32 (*enable)(struct clv_output *output);
	s32 (*disable)(struct clv_output *output);

	struct list_head link;
};

struct clv_head {
	struct clv_compositor *c;
	struct list_head link;
	struct clv_output *output;
	s32 connected;
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
	clockid_t (*get_clock_type)(struct clv_compositor *c);
};

s32 renderer_create(struct clv_compositor *c, s32 *formats, s32 count_fmts,
		    s32 no_winsys, void *native_window, s32 *vid);

void set_renderer_dbg(u8 flag);

#define NSEC_PER_SEC 1000000000

/* Subtract timespecs
 *
 * \param r[out] result: a - b
 * \param a[in] operand
 * \param b[in] operand
 */
static inline void timespec_sub(struct timespec *r,
				const struct timespec *a,
				const struct timespec *b)
{
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static inline s64 millihz_to_nsec(u32 mhz)
{
	assert(mhz > 0);
	return 1000000000000LL / mhz;
}

#endif

