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

struct clv_renderer;
struct clv_surface;
struct clv_view;
struct clv_backend;
struct clv_output;
struct clv_head;
struct clv_plane;
struct clv_server;

struct clv_display {
	struct clv_event_loop *loop;
	s32 exit;
	struct list_head clients;
};

struct clv_event_loop *clv_display_get_event_loop(struct clv_display *display);
struct clv_display *clv_display_create(void);
void clv_display_run(struct clv_display *display);
void clv_display_stop(struct clv_display *display);
void clv_display_destroy(struct clv_display *display);

struct clv_plane {
	struct clv_compositor *c;
	struct list_head link;
	/* struct clv_region damage; */
};

struct clv_compositor {
	struct clv_display *display;

	struct clv_backend *backend;

	struct clv_renderer *renderer;

	struct clv_plane root_plane;
	struct list_head views;
	struct list_head outputs;
	struct list_head pending_outputs;
	struct list_head heads;
	struct list_head planes;
	clockid_t clock_type;

	/* compositor signals */
	struct clv_signal destroy_signal;

	/* output signals */
	struct clv_signal output_created_signal;
	struct clv_signal output_destroyed_signal;
	struct clv_signal output_resized_signal;

	/* head change signals */
	struct clv_signal head_changed_signal;
	struct clv_event_source head_changed_source;

	/* repaint */
	struct clv_event_source repaint_timer_source;
};

struct clv_compositor *clv_compositor_create(struct clv_display *display);

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

	/* establish a display path (GLES context + LCDC + Head) */
	struct clv_output * (*output_create)(struct clv_compositor *c,
					     u32 head_index);
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


#endif

