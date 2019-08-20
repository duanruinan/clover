/*
 * Copyright (C) 2019 Ruinan Duan, duanruinan@zoho.com 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef CLOVER_COMPOSITOR_H
#define CLOVER_COMPOSITOR_H

#include <time.h>
#include <assert.h>
#include <clover_utils.h>
#include <clover_region.h>
#include <clover_shm.h>
#include <clover_signal.h>
#include <clover_event.h>
#include <clover_protocal.h>

struct clv_renderer;
struct clv_surface;
struct clv_view;
struct shm_buffer;
struct clv_backend;
struct clv_output;
struct clv_head;
struct clv_plane;
struct clv_server;

enum clv_desktop_mode {
	CLV_DESKTOP_DUPLICATED = 0,
	CLV_DESKTOP_EXTENDED,
};

struct clv_layer_config {
	u32 index;
	u32 type;
	s32 count_formats;
	char formats[64][64];
	struct list_head link;
};

struct clv_output_config {
	u32 index;
	s32 count_layers;
	u32 max_w, max_h;
	struct clv_layer_config layers[16];
	struct clv_layer_config *primary_layer;
	struct list_head overlay_layers;
	struct clv_layer_config *cursor_layer;
	struct clv_rect render_area;
};

struct clv_encoder_config {
	u32 index;
	struct clv_output_config output;
};

struct clv_head_config {
	u32 index;
	u32 max_w, max_h;
	struct clv_encoder_config encoder;
};

struct clv_config {
	enum clv_desktop_mode mode;
	s32 count_heads;
	struct clv_head_config heads[8];
};

struct clv_config *load_config_from_file(const char *xml);

struct clv_client_agent {
	s32 sock;
	struct clv_compositor *c;
	struct list_head link;
	struct clv_surface *surface;
	struct clv_view *view;
	struct list_head buffers;
	struct clv_event_source *client_source;
	s32 f;

	u8 *ipc_rx_buf;
	u32 ipc_rx_buf_sz;

	u8 *surface_id_created_tx_cmd_t;
	u8 *surface_id_created_tx_cmd;
	u32 surface_id_created_tx_len;

	u8 *view_id_created_tx_cmd_t;
	u8 *view_id_created_tx_cmd;
	u32 view_id_created_tx_len;

	u8 *bo_id_created_tx_cmd_t;
	u8 *bo_id_created_tx_cmd;
	u32 bo_id_created_tx_len;

	u8 *commit_ack_tx_cmd_t;
	u8 *commit_ack_tx_cmd;
	u32 commit_ack_tx_len;

	u8 *bo_complete_tx_cmd_t;
	u8 *bo_complete_tx_cmd;
	u32 bo_complete_tx_len;

	u8 *hpd_tx_cmd_t;
	u8 *hpd_tx_cmd;
	u32 hpd_tx_len;

	u8 *destroy_ack_tx_cmd_t;
	u8 *destroy_ack_tx_cmd;
	u32 destroy_ack_tx_len;
};

struct clv_display {
	struct clv_event_loop *loop;
	s32 exit;
};

#define PLANE_NAME_LEN 16
struct clv_plane {
	struct clv_compositor *c;
	u32 index;
	struct clv_output *output;
	struct list_head link;
	char name[PLANE_NAME_LEN]; /* C--X / P-X / O-X */
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
	u32 output_mask;
	struct clv_output *primary_output;
	struct clv_listener flip_listener;
	s32 is_bg;
	struct clv_client_agent *agent;
};

struct clv_view {
	enum clv_view_type type;
	struct clv_plane *plane;
	struct clv_surface *surface;
	struct clv_buffer *cursor_buf; /* used for cursor */
	struct list_head link;
	struct clv_rect area; /* in canvas coordinates */
	float alpha;
	void *last_dmafb;
	void *curr_dmafb;
	u32 output_mask;
	s32 painted;
	s32 need_to_draw;
#if 1
	s32 hot_x, hot_y;
#endif
};

struct clv_buffer {
	enum clv_buffer_type type;
	u32 w, h, stride;
	u32 size;
	enum clv_pixel_fmt pixel_fmt;
	s32 count_planes;
	char name[CLV_BUFFER_NAME_LEN];
	s32 fd;
	void *internal_fb;
	struct list_head link; /* link to client agent */
};

struct shm_buffer {
	struct clv_buffer base;
	struct clv_shm shm;
};

struct clv_compositor {
	struct clv_display *display;
	struct clv_signal destroy_signal;

	struct clv_backend *backend;

	clockid_t clk_id;

	struct clv_event_source *repaint_timer;

	struct clv_renderer *renderer;

	struct clv_plane primary_plane; /* fake root plane */

	struct list_head views;
	struct list_head outputs;
	struct list_head heads;
	struct list_head planes;

	/* head change signals used to invoke some callbacks */
	struct clv_signal heads_changed_signal;
	/* idle event added by Monitor's Hotplug */
	struct clv_event_source *heads_changed_source;

	struct clv_surface bg_surf;
	struct clv_view bg_view;
	struct shm_buffer bg_buf;

	struct clv_surface dummy_cursor_surf;
	struct clv_view dummy_cursor_view;
};

struct clv_backend {
	void (*destroy)(struct clv_compositor *c);

	/*
	 * Begin a repaint sequence
	 *
	 * alloc new pending state (repaint_data)
	 *
	 * The repaint cycle is luanched by compositor's repaint_time_source.
	 *
	 * The timer is shared by different outputs.
	 * The timer is reloaded at the end of the timer procedure.
	 * The timer is reloaded at the page flip handler.
	 *     The timestamp of the last frame is produced by page flip handler.
	 *     Use this time stamp and the frame period which is produced by the
	 *     LCDC timing to get the time of the next frame.
	 * Condition: Check each output's state, if no repainting request
	 *            is scheduled, do nothing.
	 */
	void * (*repaint_begin)(struct clv_compositor *c);

	/*
	 * Conclude a repaint sequence
	 *
	 * apply pending state (e.g. KMS's Atomic submit)\
	 */
	void (*repaint_flush)(struct clv_compositor *c, void *repaint_data);

	/*
	 * Cancel a repaint sequence
	 *
	 * free pending state (repaint data)
	 */
	void (*repaint_cancel)(struct clv_compositor *c, void *repaint_data);

	/*
	 * establish a display path (LCDC + Signal Encoder + Head)
	 * e.g. GLES output -> (Scanout Plane + Cursor Plane) CRTC0 ->
	 *          HDMI Encoder -> HDMI Connector
	 */
	struct clv_output * (*output_create)(struct clv_compositor *c,
					     struct clv_head_config *head_cfg);
	void * (*import_dmabuf)(struct clv_compositor *c,
				struct clv_buffer *buffer);
	void (*dmabuf_destroy)(struct clv_output *output, void *buffer);
};

void set_scanout_dbg(u32 flag);

#define MODE_PREFERRED 0x00000001

struct clv_mode {
	u32 flags;
	u32 w, h;
	u32 refresh;
	struct list_head link;
};

struct clv_head {
	struct clv_compositor *c;
	struct list_head link;
	struct clv_output *output;
	s32 connected;
	s32 changed;
	s32 index;
	void (*retrieve_modes)(struct clv_head *head);
};

enum clv_dpms {
	CLV_DPMS_ON,
	CLV_DPMS_OFF,
};

struct clv_output {
	struct clv_compositor *c;
	u32 index;
	struct clv_head *head;

	enum {
		REPAINT_NOT_SCHEDULED = 0,
		REPAINT_BEGIN_FROM_IDLE,
		REPAINT_SCHEDULED,
		REPAINT_AWAITING_COMPLETION,
	} repaint_status;
	struct clv_event_source *idle_repaint_source;
	s32 repaint_needed;
	s32 repaint_pending;
	struct timespec next_repaint;
	s32 repainted;

	void *renderer_state;
	struct clv_rect render_area; /* in canvas coordinates */
	s32 changed;

	struct clv_mode *current_mode;
	struct list_head modes;

	s32 enabled;

	s32 primary_dirty;

	struct clv_signal flip_signal;

	void (*start_repaint_loop)(struct clv_output *output);
	s32 (*repaint)(struct clv_output *output, void *repaint_data);
	void (*assign_planes)(struct clv_output *output, void *repaint_data);
	void (*destroy)(struct clv_output *output);
	void (*enable)(struct clv_output *output, struct clv_rect *render_area);
	s32 (*disable)(struct clv_output *output);

	struct list_head link;
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
	void (*release_dmabuf)(struct clv_compositor *c,
			       struct clv_buffer *buffer);
	void (*output_destroy)(struct clv_output *output);
	clockid_t (*get_clock_type)(struct clv_compositor *c);
};

struct clv_server {
	struct clv_display *display;
	struct clv_event_loop *loop;
	struct clv_event_source *sig_int_source, *sig_tem_source;
	struct clv_config *config;
	struct clv_compositor *c;
	struct clv_listener hpd_listener;
	s32 count_outputs;
	struct clv_output *outputs[8];

	struct list_head client_agents;
	s32 server_sock;
	struct clv_event_source *server_source;

	u8 *linkid_created_ack_tx_cmd_t;
	u8 *linkid_created_ack_tx_cmd;
	u32 linkid_created_ack_tx_len;
};

s32 renderer_create(struct clv_compositor *c, s32 *formats, s32 count_fmts,
		    s32 no_winsys, void *native_window, s32 *vid);

void set_renderer_dbg(u32 flag);

#define NSEC_PER_SEC 1000000000
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

static inline s64 timespec_to_nsec(const struct timespec *a)
{
	return (s64)a->tv_sec * NSEC_PER_SEC + a->tv_nsec;
}

static inline void timespec_add_nsec(struct timespec *r,
				     const struct timespec *a, s64 b)
{
	r->tv_sec = a->tv_sec + (b / NSEC_PER_SEC);
	r->tv_nsec = a->tv_nsec + (b % NSEC_PER_SEC);

	if (r->tv_nsec >= NSEC_PER_SEC) {
		r->tv_sec++;
		r->tv_nsec -= NSEC_PER_SEC;
	} else if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static inline void timespec_add_msec(struct timespec *r,
				     const struct timespec *a, s64 b)
{
	return timespec_add_nsec(r, a, b * 1000000);
}

static inline s64 timespec_sub_to_nsec(const struct timespec *a,
				       const struct timespec *b)
{
	struct timespec r;
	timespec_sub(&r, a, b);
	return timespec_to_nsec(&r);
}

static inline s64 timespec_sub_to_msec(const struct timespec *a,
				       const struct timespec *b)
{
	return timespec_sub_to_nsec(a, b) / 1000000;
}

struct clv_compositor *clv_compositor_create(struct clv_display *display);
void clv_compositor_destroy(struct clv_compositor *c);
void clv_compositor_add_heads_changed_listener(struct clv_compositor *c,
					       struct clv_listener *listener);
void clv_compositor_schedule_heads_changed(struct clv_compositor *c);
struct clv_event_loop *clv_display_get_event_loop(struct clv_display *display);
struct clv_display *clv_display_create(void);
void clv_display_run(struct clv_display *display);
void clv_display_stop(struct clv_display *display);
void clv_display_destroy(struct clv_display *display);
void clv_compositor_choose_mode(struct clv_output *output,
				struct clv_head_config *head_cfg);
void clv_output_schedule_repaint(struct clv_output *output, s32 cnt);
void clv_output_schedule_repaint_reset(struct clv_output *output);
void clv_compositor_schedule_repaint(struct clv_compositor *c);
void clv_surface_schedule_repaint(struct clv_surface *surface);
void clv_view_schedule_repaint(struct clv_view *view);
void clv_output_finish_frame(struct clv_output *output, struct timespec *stamp);
void clv_surface_destroy(struct clv_surface *s);
struct clv_surface *clv_surface_create(struct clv_compositor *c,
				       struct clv_surface_info *si,
				       struct clv_client_agent *agent);
void clv_view_destroy(struct clv_view *v);
struct clv_view *clv_view_create(struct clv_surface *s,
				 struct clv_view_info *vi);
void clv_surface_add_flip_listener(struct clv_surface *s);

struct clv_client_agent *client_agent_create(
	struct clv_server *s,
	s32 sock,
	s32 (*client_sock_cb)(s32 fd, u32 mask, void *data));
void client_agent_destroy(struct clv_client_agent *agent);
struct clv_buffer *shm_buffer_create(struct clv_bo_info *bi);
void shm_buffer_destroy(struct clv_buffer *buffer);
void set_compositor_dbg(u32 flags);

#endif

