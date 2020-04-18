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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <libudev.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/videodev2.h>
#include <drm_fourcc.h>
#include <drm/drm.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <clover_utils.h>
#include <clover_event.h>
#include <clover_log.h>
#include <clover_region.h>
#include <clover_compositor.h>

#ifndef CONFIG_ROCKCHIP_DRM_HWC
#define CONFIG_ROCKCHIP_DRM_HWC
#endif

static u8 drm_dbg = 5;
static u8 gbm_dbg = 5;
static u8 timer_dbg = 5;
static u8 ps_dbg = 5;

struct clv_renderer *gl_renderer = NULL;

#define drm_debug(fmt, ...) do { \
	if (drm_dbg >= 3) { \
		clv_debug("[DRM ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define drm_info(fmt, ...) do { \
	if (drm_dbg >= 2) { \
		clv_info("[DRM ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define drm_notice(fmt, ...) do { \
	if (drm_dbg >= 1) { \
		clv_notice("[DRM ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define drm_warn(fmt, ...) do { \
	clv_warn("[DRM ] " fmt, ##__VA_ARGS__); \
} while (0);

#define drm_err(fmt, ...) do { \
	clv_err("[DRM ] " fmt, ##__VA_ARGS__); \
} while (0);

#define gbm_debug(fmt, ...) do { \
	if (gbm_dbg >= 3) { \
		clv_debug("[GBM ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define gbm_info(fmt, ...) do { \
	if (gbm_dbg >= 2) { \
		clv_info("[GBM ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define gbm_notice(fmt, ...) do { \
	if (gbm_dbg >= 1) { \
		clv_notice("[GBM ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define gbm_warn(fmt, ...) do { \
	clv_warn("[GBM ] " fmt, ##__VA_ARGS__); \
} while (0);

#define gbm_err(fmt, ...) do { \
	clv_err("[GBM ] " fmt, ##__VA_ARGS__); \
} while (0);

#define ps_debug(fmt, ...) do { \
	if (ps_dbg >= 3) { \
		clv_debug("[PS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define ps_info(fmt, ...) do { \
	if (ps_dbg >= 2) { \
		clv_info("[PS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define ps_notice(fmt, ...) do { \
	if (ps_dbg >= 1) { \
		clv_notice("[PS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define ps_warn(fmt, ...) do { \
	clv_warn("[PS  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define ps_err(fmt, ...) do { \
	clv_err("[PS  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define timer_debug(fmt, ...) do { \
	if (timer_dbg >= 3) { \
		clv_debug("[TS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define timer_info(fmt, ...) do { \
	if (timer_dbg >= 2) { \
		clv_info("[TS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define timer_notice(fmt, ...) do { \
	if (timer_dbg >= 1) { \
		clv_notice("[TS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define timer_warn(fmt, ...) do { \
	clv_warn("[TS  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define timer_err(fmt, ...) do { \
	clv_err("[TS  ] " fmt, ##__VA_ARGS__); \
} while (0);

struct drm_backend;
struct drm_output;
struct drm_head;
struct drm_plane;

struct drm_mode {
	struct clv_mode base;
	drmModeModeInfo mode_info;
	u32 blob_id;
};

enum drm_fb_type {
	DRM_BUF_INVALID = 0,
	DRM_BUF_DMABUF, /* for overlay plane */
	DRM_BUF_GBM_SURFACE, /* internal EGL rendering */
	DRM_BUF_CURSOR, /* internal cursor */
};

struct drm_fb {
	enum drm_fb_type type;
	s32 refcnt;
	u32 fb_id, size;
	u32 handles[4];
	u32 strides[4];
	u32 offsets[4];
	u32 w, h;
	s32 fd;
	s32 dma_fd;
	u32 drm_fmt;

	struct gbm_bo *bo;
	struct gbm_surface *gbm_surface;
};

struct drm_pending_state {
	struct drm_backend *b;
	struct list_head output_states;
};

struct drm_output_state {
	struct drm_pending_state *pending_state;
	struct drm_output *output;
	enum clv_dpms dpms;
	struct list_head link;
	struct list_head plane_states;
};

struct drm_plane_state {
	struct drm_plane *plane;
	struct drm_output *output;
	struct drm_output_state *output_state;

	struct drm_fb *fb;

	struct clv_view *v;

	s32 src_x, src_y;
	u32 src_w, src_h;
	s32 crtc_x, crtc_y;
	u32 crtc_w, crtc_h;

	s32 complete; /* whether plane request is completed by kernel or not */

	struct list_head link;
};

enum drm_plane_type {
	DRM_OVERLAY_PL = DRM_PLANE_TYPE_OVERLAY,
	DRM_PRIMARY_PL = DRM_PLANE_TYPE_PRIMARY,
	DRM_CURSOR_PL = DRM_PLANE_TYPE_CURSOR,
};

struct drm_encoder {
	struct drm_backend *b;
	u32 index;
	u32 encoder_id;
	drmModeEncoderPtr enc;

	struct drm_output *output;
	struct drm_head *head;
};

struct drm_plane {
	struct clv_plane base;
	struct drm_backend *b;
	u32 index;
	u32 plane_id;
	drmModePlanePtr plane;
	drmModeObjectProperties *props;
	u32 prop_crtc_id;
	u32 prop_fb_id;
	u32 prop_crtc_x;
	u32 prop_crtc_y;
	u32 prop_crtc_w;
	u32 prop_crtc_h;
	u32 prop_src_x;
	u32 prop_src_y;
	u32 prop_src_w;
	u32 prop_src_h;
	u32 prop_color_space;

	enum drm_plane_type type;

	struct list_head output_link;
	struct list_head link;

	struct drm_output *output;

	/* The last state submitted to the kernel for this plane. */
	struct drm_plane_state *state_cur;
};

struct drm_head {
	struct clv_head base;
	struct drm_backend *b;
	u32 index;
	u32 connector_id;
	drmModeConnectorPtr connector;
	drmModeObjectProperties *props;
	u32 prop_crtc_id;
	u32 prop_hdmi_quant_range;

	struct list_head link;

	struct drm_output *output;
	struct drm_encoder *encoder;

	struct clv_event_source *hpd_detect_source;
};

struct drm_output {
	struct clv_output base;
	struct drm_backend *b;
	u32 index;
	u32 crtc_id;
	drmModeCrtcPtr crtc;
	drmModeObjectProperties *props;

	u32 prop_active;
	u32 prop_mode_id;

	struct list_head link;

	struct drm_head *head;
	struct drm_encoder *encoder;

	struct list_head planes;
	struct drm_plane *primary_plane;
	struct drm_plane *cursor_plane;
	struct drm_plane *overlay_plane;

	struct drm_fb *cursor_fb[2];
	s32 cursor_index;

	s32 disable_pending;
	s32 atomic_complete_pending;

	struct gbm_surface *gbm_surface;
	uint32_t gbm_bo_flags;

	/* The last state submitted to the kernel for this CRTC. */
	struct drm_output_state *state_cur;
	/* The previously-submitted state, where the hardware has not
	 * yet acknowledged completion of state_cur. */
	struct drm_output_state *state_last;

#ifdef CONFIG_ROCKCHIP_DRM_HWC
	/*
	 * for cursor which has hot_x/hot_y.
	 */
	s32 cursor_relocate;
#endif
};

struct drm_backend {
	struct clv_backend base;
	struct clv_compositor *c;

	struct clv_event_loop *loop;

	struct clv_region canvas;

	char dev_node[128];
	s32 fd;

	u32 cursor_width, cursor_height;

	struct udev *udev;
	struct clv_event_source *drm_source;

	struct udev_monitor *udev_monitor;
	struct clv_event_source *udev_drm_source;
	s32 sysnum;

	struct gbm_device *gbm;
	u32 gbm_format;

	void *repaint_data;

	s32 state_invalid;

	drmModeRes *res;
	drmModePlaneRes *pres;

	struct list_head outputs;
	struct list_head planes;
	struct list_head heads;
};

static struct drm_backend *to_drm_backend(struct clv_compositor *c)
{
	return container_of(c->backend, struct drm_backend, base);
}

static struct drm_output *to_drm_output(struct clv_output *output)
{
	return container_of(output, struct drm_output, base);
}

static struct drm_head *to_drm_head(struct clv_head *head)
{
	return container_of(head, struct drm_head, base);
}

static struct drm_mode *to_drm_mode(struct clv_mode *mode)
{
	return container_of(mode, struct drm_mode, base);
}

static s32 set_drm_caps(struct drm_backend *b)
{
	u64 cap;
	s32 ret;
	
	ret = drmGetCap(b->fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1) {
		drm_info("DRM TIMESTAMP: MONOTONIC");
		b->c->clk_id = CLOCK_MONOTONIC;
	} else {
		drm_info("DRM TIMESTAMP: REALTIME");
		b->c->clk_id = CLOCK_REALTIME;
	}

	if (drmSetClientCap(b->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		drm_err("DRM_CLIENT_CAP_UNIVERSAL_PLANES not supported");
		return -1;
	} else {
		drm_info("DRM_CLIENT_CAP_UNIVERSAL_PLANES supported");
	}

	if (drmSetClientCap(b->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		drm_err("DRM_CLIENT_CAP_ATOMIC not supported");
		return -1;
	} else {
		drm_info("DRM_CLIENT_CAP_ATOMIC supported");
	}

	ret = drmGetCap(b->fd, DRM_CAP_CURSOR_WIDTH, &cap);
	if (ret) {
		drm_warn("DRM_CAP_CURSOR_WIDTH not supported");
		b->cursor_width = 64;
	} else {
		drm_info("DRM_CAP_CURSOR_WIDTH: %lu", cap);
		b->cursor_width = cap;
	}

	ret = drmGetCap(b->fd, DRM_CAP_CURSOR_HEIGHT, &cap);
	if (ret) {
		drm_warn("DRM_CAP_CURSOR_HEIGHT not supported");
		b->cursor_height = 64;
	} else {
		drm_info("DRM_CAP_CURSOR_HEIGHT: %lu", cap);
		b->cursor_height = cap;
	}

	return 0;
}

static void drm_fb_destroy(struct drm_fb *fb);

static void drm_fb_destroy_gbm(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;

	assert(fb->type == DRM_BUF_GBM_SURFACE || fb->type == DRM_BUF_CURSOR);
	drm_fb_destroy(fb);
}

static s32 drm_fb_addfb(struct drm_backend *b, struct drm_fb *fb)
{
	return drmModeAddFB2(fb->fd, fb->w, fb->h, fb->drm_fmt,
			     fb->handles, fb->strides, fb->offsets, &fb->fb_id,
			     0);
}

static struct drm_fb *drm_fb_ref(struct drm_fb *fb);

static struct drm_fb *drm_fb_get_from_bo(struct gbm_bo *bo,
					 struct drm_backend *b,
					 s32 is_opaque,
					 enum drm_fb_type type)
{
	struct drm_fb *fb = gbm_bo_get_user_data(bo);

	if (fb) {
		assert(fb->type == type);
		return drm_fb_ref(fb);
	}

	fb = calloc(1, sizeof(*fb));
	if (fb == NULL)
		return NULL;

	fb->type = type;
	fb->refcnt = 1;
	fb->bo = bo;
	fb->fd = b->fd;

	fb->w = gbm_bo_get_width(bo);
	fb->h = gbm_bo_get_height(bo);
	fb->drm_fmt = gbm_bo_get_format(bo);
	fb->size = 0;

	fb->strides[0] = gbm_bo_get_stride(bo);
	fb->handles[0] = gbm_bo_get_handle(bo).u32;

	if (is_opaque) {
		if (fb->drm_fmt == DRM_FORMAT_ARGB8888) {
			drm_err("replace DRM_FORMAT_ARGB8888 with XRGB8888");
			fb->drm_fmt = DRM_FORMAT_XRGB8888;
		}		
	}

	if (drm_fb_addfb(b, fb) != 0) {
		if (type == DRM_BUF_GBM_SURFACE)
			drm_err("failed to create kms fb: %s", strerror(errno));
		goto err_free;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_gbm);

	drm_debug("[FB] ref+ fb get from bo %p", fb);
	return fb;

err_free:
	free(fb);
	return NULL;
}

static void drm_fb_destroy(struct drm_fb *fb)
{
	if (fb->fb_id != 0)
		drmModeRmFB(fb->fd, fb->fb_id);
	free(fb);
}

static struct drm_fb *drm_fb_ref(struct drm_fb *fb)
{
	if (!fb) {
		drm_warn("fb == NULL");
		return NULL;
	}

	drm_debug("[FB] ref+ %p", fb);
	drm_debug("[FB] ref result %u", fb->refcnt+1);
	fb->refcnt++;

	return fb;
}

static void drm_fb_destroy_dmabuf(struct drm_fb *fb)
{
	struct drm_gem_close req;

	if (fb->handles[0]) {
		drm_debug("close GEM");
		memset(&req, 0, sizeof(req));
		req.handle = fb->handles[0];
		drmIoctl(fb->fd, DRM_IOCTL_GEM_CLOSE, &req);
	}
	drm_fb_destroy(fb);
}

static struct drm_fb *drm_fb_get_from_dmabuf(struct clv_buffer *buffer,
					     struct drm_backend *b)
{
	struct drm_fb *fb;
	s32 ret;
	u32 w_align, h_align;

	if (!buffer)
		return NULL;

	fb = calloc(1, sizeof(*fb));
	if (!fb)
		return NULL;

	fb->refcnt = 1;
	fb->type = DRM_BUF_DMABUF;

	ret = drmPrimeFDToHandle(b->fd, buffer->fd, &fb->handles[0]);
	if (ret) {
		drm_err("drmPrimeFDToHandle failed. fd = %d. %s", buffer->fd,
			strerror(errno));
		free(fb);
		return NULL;
	}

	fb->w = buffer->w;
	fb->h = buffer->h;
	if (buffer->vstride) {
		w_align = buffer->stride;
		h_align = buffer->vstride;
	} else {
		w_align = (buffer->w + 16 - 1) & ~(16 - 1);
		h_align = (buffer->h + 16 - 1) & ~(16 - 1);
	}
	fb->dma_fd = buffer->fd;
	fb->fd = b->fd;
	fb->bo = NULL;

	switch (buffer->pixel_fmt) {
	case CLV_PIXEL_FMT_NV12:
		fb->drm_fmt = DRM_FORMAT_NV12;
		fb->size = w_align * h_align * 3 / 2;
		fb->offsets[0] = 0;
		fb->strides[0] = w_align;
		fb->handles[1] = fb->handles[0];
		fb->offsets[1] = w_align * h_align;
		fb->strides[1] = w_align;
		break;
	case CLV_PIXEL_FMT_NV24:
		fb->drm_fmt = DRM_FORMAT_NV24;
		fb->size = w_align * h_align * 3;
		fb->offsets[0] = 0;
		fb->strides[0] = w_align;
		fb->handles[1] = fb->handles[0];
		fb->offsets[1] = w_align * h_align;
		drm_debug("offset = %u\n", fb->offsets[1]);
		fb->strides[1] = w_align * 2;
		drm_debug("w_align = %u h_align = %u size = %u",
			w_align, h_align, fb->size);
		break;
	default:
		drm_err("unknown pixel_fmt %u", buffer->pixel_fmt);
		free(fb);
		return NULL;
	}

	if (drm_fb_addfb(b, fb) < 0) {
		drm_err("drm_fb_addfb failed.");
		free(fb);
		return NULL;
	}

	buffer->internal_fb = fb;

	return fb;
}

static void drm_fb_unref(struct drm_fb *fb)
{
	if (!fb) {
		drm_warn("fb == NULL");
		return;
	}
	drm_debug("[FB] unref- %p", fb);
	drm_debug("[FB] unref result %d", fb->refcnt - 1);

	//assert(fb->refcnt > 0);
	if (fb->refcnt <= 0) {
		clv_err("fb->refcnt <= 0 %d %p", fb->refcnt, fb);
		getchar();
	}
	if (--fb->refcnt > 0)
		return;

	switch (fb->type) {
	case DRM_BUF_CURSOR:
		drm_debug("[FB] Real destroy cursor bo");
		gbm_bo_destroy(fb->bo);
		break;
	case DRM_BUF_GBM_SURFACE:
		drm_debug("[FB] Real destroy gbm bo");
		gbm_surface_release_buffer(fb->gbm_surface, fb->bo);
		break;
	case DRM_BUF_DMABUF:
		drm_debug("[FB] Real destroy dmabuf bo");
		drm_fb_destroy_dmabuf(fb);
		break;
	default:
		assert(NULL);
		break;
	}
}

static void drm_plane_state_free(struct drm_plane_state *state, s32 force);

static void drm_dmabuf_destroy(struct clv_output *out, void *buffer)
{
	struct drm_fb *fb = buffer;
	struct drm_output *output;
	struct drm_output_state *state;
	struct drm_plane_state *ps, *next;
	struct drm_backend *b;

	drm_debug("destroy dmabuf %p", buffer);
	if (!fb) {
		drm_warn("fb == NULL");
		return;
	}
	//fb->refcnt = 1;

	b = to_drm_backend(out->c);
	list_for_each_entry(output, &b->outputs, link) {
		if (output->state_last) {
			ps_debug("last plane state: output (%p)", output);
			state = output->state_last;
			list_for_each_entry_safe(ps, next,
						 &state->plane_states, link) {
				drm_debug("ps->fb = %p, ps->plane->type = %u",
					  ps->fb, ps->plane->type);
			}
		}

		if (output->state_cur) {
			ps_debug("current plane state: output (%p)", output);
			state = output->state_cur;
			list_for_each_entry_safe(ps, next,
						 &state->plane_states, link) {
				drm_debug("ps->fb = %p, ps->plane->type = %u",
					  ps->fb, ps->plane->type);
				if (ps->fb == buffer) {
					ps->plane->state_cur = NULL;
					drm_plane_state_free(ps, 1);
				}
			}
		}
	}

	drm_fb_unref(fb);
}

static void *drm_import_dmabuf(struct clv_compositor *c,
			       struct clv_buffer *buffer)
{
	struct drm_backend *b = to_drm_backend(c);

	if (!buffer)
		return NULL;

	if (buffer->internal_fb) {
//		return drm_fb_ref(buffer->internal_fb);
		return buffer->internal_fb;
	}
	return drm_fb_get_from_dmabuf(buffer, b);
}

static s32 drm_output_disable(struct clv_output *base);
static void drm_output_state_free(struct drm_output_state *state);

static void drm_output_update_complete(struct drm_output *output,
				       u32 sec, u32 usec)
{
	struct drm_plane_state *state;
	struct timespec ts;

	list_for_each_entry(state, &output->state_cur->plane_states, link)
		state->complete = 1;

	drm_output_state_free(output->state_last);
	output->state_last = NULL;

	drm_debug("output->disable_pending = %u", output->disable_pending);
	if (output->disable_pending) {
		drm_output_disable(&output->base);
		output->disable_pending = 0;
		return;
	} else if (output->state_cur->dpms == CLV_DPMS_OFF &&
	           output->base.repaint_status != REPAINT_AWAITING_COMPLETION) {
		/* DPMS can happen to us either in the middle of a repaint
		 * cycle (when we have painted fresh content, only to throw it
		 * away for DPMS off), or at any other random point. If the
		 * latter is true, then we cannot go through finish_frame,
		 * because the repaint machinery does not expect this. */
		assert(0);
		return;
	}

	ts.tv_sec = sec;
	ts.tv_nsec = usec * 1000;
	timer_debug("[OUTPUT: %u] now: %ld, %d",
		  output->index, ts.tv_sec, usec);
	clv_output_finish_frame(&output->base, &ts);
}

static void page_flip_handler(s32 fd, u32 crtc_id, u32 frame, u32 sec, u32 usec,
			      void *data)
{
	struct drm_backend *b = data;
	struct drm_output *output;
	s32 find = 0;
//	struct timespec now;
	
	drm_debug("crtc_id = %u", crtc_id);
	list_for_each_entry(output, &b->outputs, link) {
		if (output->crtc_id == crtc_id) {
			find = 1;
			break;
		}
	}
	assert(find);
//	clock_gettime(b->c->clk_id, &now);
//	clv_debug("[atomic] [CRTC: %u] page flip processing started, %ld, %ld",
//		  crtc_id,
//		  now.tv_sec, now.tv_nsec / 1000000l);
	drm_debug("[atomic] [CRTC: %u] page flip processing started", crtc_id);
	assert(output->atomic_complete_pending);
	output->atomic_complete_pending = 0;
	drm_output_update_complete(output, sec, usec);
//	clock_gettime(b->c->clk_id, &now);
//	clv_debug("[atomic] [CRTC: %u] page flip processing end, %ld, %ld",
//		  crtc_id,
//		  now.tv_sec, now.tv_nsec / 1000000l);
	drm_debug("[atomic] [CRTC: %u] page flip processing end", crtc_id);
}

static void vblank_handler(s32 fd, u32 frame, u32 sec, u32 usec, void *data)
{
	assert(0);
}

static s32 drm_event_proc(s32 fd, u32 mask, void *data)
{
	//struct drm_backend *b = data;
	drmEventContext ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.version = 3;
	ctx.page_flip_handler2 = page_flip_handler;
	ctx.vblank_handler = vblank_handler;
	drmHandleEvent(fd, &ctx);
	return 0;
}

static s32 get_drm_dev_sysnum(struct udev *udev, const char *devname, s32 *sn)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	const char *path, *sysnum, *devnode;
	struct udev_device *drm_device = NULL;
	s32 ret;

	if (!udev)
		return -EINVAL;

	if (!devname)
		return -EINVAL;

	if (!sn)
		return -EINVAL;

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "drm");
	udev_enumerate_add_match_sysname(e, "card[0-9]*");
	udev_enumerate_scan_devices(e);
	drm_device = NULL;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		drm_device = udev_device_new_from_syspath(udev, path);
		if (!drm_device) {
			continue;
		} else {
			devnode = udev_device_get_devnode(drm_device);
			if (!devnode) {
				udev_device_unref(drm_device);
				continue;
			}
			if (strcmp(devnode, devname)) {
				udev_device_unref(drm_device);
				continue;
			} else {
				sysnum = udev_device_get_sysnum(drm_device);
				if (!sysnum) {
					clv_err("get_sysnum failed. %s",
					    strerror(errno));
					udev_device_unref(drm_device);
					drm_device = NULL;
					goto err;
				}
				*sn = atoi(sysnum);
				drm_info("%s's sysnum = %d", devnode, *sn);
				udev_device_unref(drm_device);
				break;
			}
		}
	}

err:
	udev_enumerate_unref(e);
	if (drm_device)
		ret = 0;
	else
		ret = -EFAULT;
	return ret;
}

static s32 hpd_detect_proc(void *data)
{
	struct drm_head *head = data;
	struct clv_compositor *c = head->b->c;
	struct timespec now;

	/* set head's connection state as disconnect & stop timer source */
	head->base.changed = 1;
	head->base.connected = 0;
	clv_event_source_timer_update(head->hpd_detect_source, 0, 0);
	clock_gettime(c->clk_id, &now);
	drm_debug("<<< update head %u's status to disconnected %ld, %ld",
		  head->index, now.tv_sec, now.tv_nsec);
	clv_compositor_schedule_heads_changed(c);
	return 0;
}

static void drm_schedule_hpd_detect(struct drm_head *head, s32 connected)
{
	struct clv_compositor *c = head->b->c;
	struct timespec now;

	if (connected) {
		if (head->base.connected) {
			/*
			 * the last state of disconnection is not a real
			 * disconnection.
			 */
			clock_gettime(c->clk_id, &now);
			drm_debug("<<< ignore head %u's HPD jitter %ld, %ld>>>",
				  head->index, now.tv_sec, now.tv_nsec);
			clv_event_source_timer_update(head->hpd_detect_source,
						      0, 0);
		} else {
			head->b->state_invalid = 1;
			head->base.changed = 1;
			head->base.connected = 1;
			clv_compositor_schedule_heads_changed(c);
			drm_debug("<<< update head %u's status to connected",
				  head->index);
		}
	} else {
		/* start timer */
		assert(head->hpd_detect_source);
		clock_gettime(c->clk_id, &now);
		drm_debug("<<< update head %u's hpd timer %ld, %ld >>>",
			  head->index, now.tv_sec, now.tv_nsec);
		clv_event_source_timer_update(head->hpd_detect_source, 700, 0);
	}
}

static void update_connectors(struct drm_backend *b)
{
	struct drm_output *output;
	struct drm_head *head;
	drmModeConnection old;

	list_for_each_entry(output, &b->outputs, link) {
		if (!output->head)
			continue;
		head = output->head;
		if (!head->connector)
			continue;
		old = head->connector->connection;
		drmModeFreeConnector(head->connector);
		head->connector = drmModeGetConnector(b->fd,
					b->res->connectors[head->index]);
		if (!head->connector)
			continue;

		if (old == head->connector->connection) {
			head->base.changed = 0;
			continue;
		}

		if (head->connector->connection == DRM_MODE_CONNECTED) {
			drm_schedule_hpd_detect(head, 1);
		} else {
			drm_schedule_hpd_detect(head, 0);
		}
	}
}

static s32 udev_drm_event_proc(s32 fd, u32 mask, void *data)
{
	struct udev_device *device;
	struct drm_backend *b = data;
	const char *sysnum;
	const char *val;

	device = udev_monitor_receive_device(b->udev_monitor);
	sysnum = udev_device_get_sysnum(device);
	if (!sysnum || atoi(sysnum) != b->sysnum) {
		udev_device_unref(device);
		return 0;
	}
	val = udev_device_get_property_value(device, "HOTPLUG");
	if (val && (!strcmp(val, "1"))) {
		drm_info("DRM Hotplug detected.");
		update_connectors(b);
	}
	udev_device_unref(device);

	return 0;
}

static void drm_pending_state_free(struct drm_pending_state *ps);

static void drm_plane_state_free(struct drm_plane_state *state, s32 force)
{
	if (!state)
		return;

	ps_debug("plane %u state free %p cur state %p", state->plane->index,
		 state, state->plane->state_cur);
	list_del(&state->link);
	INIT_LIST_HEAD(&state->link);
	state->output_state = NULL;
	if (force || state != state->plane->state_cur) {
		drm_fb_unref(state->fb);
		free(state);
	}
}

static void drm_output_state_free(struct drm_output_state *state)
{
	struct drm_plane_state *ps, *next;

	ps_debug("output state free");
	if (!state)
		return;

	ps_debug("free output %u's state", state->output->index);
	list_for_each_entry_safe(ps, next, &state->plane_states, link) {
		drm_plane_state_free(ps, 0);
	}

	list_del(&state->link);

	free(state);
}

static void drm_output_assign_state(struct drm_output_state *state,s32 is_async)
{
	struct drm_output *output = state->output;
	struct drm_plane_state *plane_state;

	ps_debug("output assign state %p is_async %d", output, is_async);

	assert(!output->state_last);

	if (is_async)
		output->state_last = output->state_cur;
	else
		drm_output_state_free(output->state_cur);

	list_del(&state->link);
	INIT_LIST_HEAD(&state->link);
	state->pending_state = NULL;

	output->state_cur = state;

	if (is_async) {
		drm_debug("\t[CRTC:%u] setting pending flip", output->crtc_id);
		output->atomic_complete_pending = 1;
	}

	list_for_each_entry(plane_state, &state->plane_states, link) {
		struct drm_plane *plane = plane_state->plane;

		if (plane->state_cur && !plane->state_cur->output_state) {
			drm_plane_state_free(plane->state_cur, 1);
		}
		ps_debug("plane %u's state_cur = %p", plane->index,
			 plane_state);
		plane->state_cur = plane_state;

		if (!is_async) {
			plane_state->complete = 1;
			continue;
		}
	}
}

static s32 drm_mode_ensure_blob(struct drm_backend *b, struct drm_mode *mode);

static s32 drm_output_apply_state_atomic(struct drm_output_state *state,
					 drmModeAtomicReq *req,
					 u32 *flags)
{
	struct drm_output *output = state->output;
	struct drm_backend *b = to_drm_backend(output->base.c);
	struct drm_plane_state *plane_state, *next;
	struct drm_mode *current_mode = to_drm_mode(output->base.current_mode);
	s32 ret = 0;

	drm_debug(":::state->dpms = %u output->state_cur->dpms = %u",
		  state->dpms, output->state_cur->dpms);
	if (state->dpms != output->state_cur->dpms) {
		*flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
		drm_debug("ModeSet!!!");
	}

	if (state->dpms == CLV_DPMS_ON) {
		drm_debug("[MODSET] DPMS is set to [ON ] ..................\n");
		ret = drm_mode_ensure_blob(b, current_mode);
		if (ret != 0)
			return ret;

		drm_debug("[MODESET] Set output %u active", output->index);
		drmModeAtomicAddProperty(req, output->crtc_id,
					 output->prop_mode_id,
					 current_mode->blob_id);
		drmModeAtomicAddProperty(req, output->crtc_id,
					 output->prop_active,
					 1);
		drmModeAtomicAddProperty(req, output->head->connector_id,
					 output->head->prop_crtc_id,
					 output->crtc_id);
		if (output->head->prop_hdmi_quant_range != ((u32)-1)) {
			drmModeAtomicAddProperty(
					req,
					output->head->connector_id,
					output->head->prop_hdmi_quant_range, 2);
		}
	} else {
		drm_debug("[MODESET] DPMS is set to [OFF] .................\n");
		drm_debug("[MODESET] Set output %u inactive", output->index);
		drmModeAtomicAddProperty(req, output->crtc_id,
					 output->prop_mode_id, 0);
		drmModeAtomicAddProperty(req, output->crtc_id,
					 output->prop_active, 0);
		drmModeAtomicAddProperty(req, output->head->connector_id,
					 output->head->prop_crtc_id, 0);
	}

	list_for_each_entry_safe(plane_state, next, &state->plane_states, link){
		struct drm_plane *plane = plane_state->plane;

		if (plane_state->fb) {
			if (!plane) {
				drm_err("plane_state %p's plane == NULL, free",
					plane_state);
				drm_plane_state_free(plane_state, 1);
				continue;
			}
			drm_debug("plane->index = %u, type = %u",
				plane->index, plane->type);
			if (plane->type == DRM_OVERLAY_PL) {
				drm_debug("need_to_draw: %p %u",
					  plane_state->v,
					  plane_state->v->need_to_draw);
				if (plane_state->v->need_to_draw) {
					plane_state->v->need_to_draw--;
					plane_state->v->painted++;
				}
			}
			drm_debug("[MODESET] Set output %u's plane %u fb_id %u",
				  output->index, plane->index,
				  plane_state->fb->fb_id);
		} else {
			drm_debug("[MODESET] Clr output %u's plane %u",
				  output->index, plane->index);
		}
//		if (plane_state->fb)
//			clv_debug("apply fb's fd %d", plane_state->fb->dma_fd);
		
		drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_fb_id,
					 plane_state->fb ?
					     plane_state->fb->fb_id : 0);

		drm_debug("plane index %u fb_id %u", plane->index,
			  plane_state->fb ? plane_state->fb->fb_id : 0);
		drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_crtc_id,
					 plane_state->fb ?
					     output->crtc_id : 0);
		drm_debug("plane index %u crtc index %u crtc_id %u",
			  plane->index, output->index, plane_state->fb ?
					     output->crtc_id : 0);
			  
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					       plane->prop_src_x,
					       plane_state->src_x);
		drm_debug("SRC_X %d %d", plane_state->src_x, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_src_y,
					 plane_state->src_y);
		drm_debug("SRC_Y %d %d", plane_state->src_y, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_src_w,
					 plane_state->src_w);
		drm_debug("SRC_W %u %d", plane_state->src_w, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_src_h,
					 plane_state->src_h);
		drm_debug("SRC_H %u %d", plane_state->src_h, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_crtc_x,
					 plane_state->crtc_x);
		drm_debug("CRTC X %d %d", plane_state->crtc_x, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_crtc_y,
					 plane_state->crtc_y);
		drm_debug("CRTC Y %d %d", plane_state->crtc_y, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_crtc_w,
					 plane_state->crtc_w);
		drm_debug("CRTC W %u %d", plane_state->crtc_w, ret);
		ret = drmModeAtomicAddProperty(req, plane->plane_id,
					 plane->prop_crtc_h,
					 plane_state->crtc_h);
		drm_debug("CRTC H %u %d", plane_state->crtc_h, ret);
		if (plane->prop_color_space != ((u32)-1)) {
			ret = drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_color_space,
						V4L2_COLORSPACE_REC709);
			drm_debug("COLOR SPACE %u %d", V4L2_COLORSPACE_REC709,
					ret);
		}
	}

	return 0;
}

static struct drm_output_state *drm_output_state_dup(
				struct drm_output_state *src,
				struct drm_pending_state *ps,
				s32 reset_plane);

static struct drm_output_state *drm_output_get_disable_state(
				struct drm_pending_state *ps,
				struct drm_output *output)
{
	struct drm_output_state *output_state;

	output_state = drm_output_state_dup(output->state_cur,
					    ps, 1);
	output_state->dpms = CLV_DPMS_OFF;

	return output_state;
}

static s32 drm_pending_state_apply_atomic(struct drm_pending_state *ps,
					  s32 is_async) 
{
	struct drm_backend *b = ps->b;
	struct drm_output_state *output_state, *tmp;
	struct drm_plane *plane;
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	u32 flags;
	s32 ret = 0;
	struct clv_head *head_base;
	struct drm_head *head;
	struct drm_output *output;
	struct timespec now, t1, t2, t3, t4, t5;

	if (!req)
		return -1;

	if (is_async) {
		flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	} else {
		flags = 0;
	}

	clock_gettime(b->c->clk_id, &t1);
	if (b->state_invalid) {
		s32 err;

		list_for_each_entry(head_base, &b->c->heads, link) {
			drm_debug("<<< head %u's status: %u >>>",
				  head_base->index, head_base->connected);
			//if (head_base->connected)
			if (head_base->output->enabled)
				continue;

			head = to_drm_head(head_base);

			drm_debug("\t\t[atomic] disabling inactive head %u",
				  head_base->index);

			err = drmModeAtomicAddProperty(req, head->connector_id,
						       head->prop_crtc_id, 0);
			if (err <= 0)
				ret = -1;

			output = head->output;
			err = drmModeAtomicAddProperty(req,
						       output->crtc_id,
						       output->prop_active, 0);
			if (err <= 0)
				ret = -1;
			err = drmModeAtomicAddProperty(req,
						       output->crtc_id,
						       output->prop_mode_id, 0);
			if (err <= 0)
				ret = -1;
		}

		list_for_each_entry(plane, &b->planes, link) {
			if (plane->output->base.enabled)
				continue;
			drm_debug("[MODESET] clear output %u's plane %u",
				  plane->output->index,
				  plane->index);
			drmModeAtomicAddProperty(req, plane->plane_id,
						 plane->prop_crtc_id, 0);
			drmModeAtomicAddProperty(req, plane->plane_id,
						 plane->prop_fb_id, 0);
		}

		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	clock_gettime(b->c->clk_id, &t2);

	list_for_each_entry(output_state, &ps->output_states, link) {
		drm_debug("apply output %u atomic", output_state->output->index);
		ret |= drm_output_apply_state_atomic(output_state, req, &flags);
	}
	clock_gettime(b->c->clk_id, &t3);

	if (ret != 0) {
		drm_err("atomic: couldn't compile atomic state");
		goto out;
	}

	clock_gettime(b->c->clk_id, &now);
	timer_debug("AtomicCommit [%s] now: %ld,%ld",
		    flags & DRM_MODE_ATOMIC_ALLOW_MODESET ? "MODSET" : "NORMAL",
		    now.tv_sec, now.tv_nsec / 1000000l);
//	clv_debug("AtomicCommit [%s] now: %ld,%ld",
//		    flags & DRM_MODE_ATOMIC_ALLOW_MODESET ? "MODSET" : "NORMAL",
//		    now.tv_sec, now.tv_nsec / 1000000l);
	ret = drmModeAtomicCommit(b->fd, req, flags, b);
	drm_debug("[atomic] drmModeAtomicCommit");
	clock_gettime(b->c->clk_id, &t4);

	if (ret != 0) {
		drm_err("atomic: couldn't commit new state: %s",
			strerror(errno));
		goto out;
	}

	drm_debug("assign state");
	list_for_each_entry_safe(output_state, tmp, &ps->output_states, link) {
		drm_debug("------- assign %u %p %p...",
			  output_state->output->index,
			  output_state, ps);
		drm_output_assign_state(output_state, is_async);
	}

	b->state_invalid = 0;

	assert(list_empty(&ps->output_states));
	clock_gettime(b->c->clk_id, &t5);
	timer_debug("drm flush spent %ld ms", timespec_sub_to_msec(&t2, &t1));
	timer_debug("drm flush spent %ld ms", timespec_sub_to_msec(&t3, &t2));
	timer_debug("drm flush spent %ld ms", timespec_sub_to_msec(&t4, &t3));
	timer_debug("drm flush spent %ld ms", timespec_sub_to_msec(&t5, &t4));
	timer_debug("is_async = %d, flags = %u", is_async, flags);

out:
	drmModeAtomicFree(req);
	drm_pending_state_free(ps);
	return ret;
}

static s32 drm_pending_state_apply_sync(struct drm_pending_state *ps)
{
	return drm_pending_state_apply_atomic(ps, 0);
}

static s32 drm_pending_state_apply(struct drm_pending_state *ps)
{
	return drm_pending_state_apply_atomic(ps, 1);
}

static struct drm_pending_state *drm_pending_state_alloc(struct drm_backend *b)
{
	struct drm_pending_state *ps;

	ps_debug("pending state alloc");
	ps = calloc(1, sizeof(*(ps)));
	if (!ps)
		return NULL;

	ps->b = b;
	INIT_LIST_HEAD(&ps->output_states);

	return ps;
}

static void drm_pending_state_free(struct drm_pending_state *ps)
{
	struct drm_output_state *output_state, *tmp;

	ps_debug("pending state free");
	if (!ps)
		return;

	list_for_each_entry_safe(output_state, tmp, &ps->output_states, link) {
		drm_output_state_free(output_state);
	}

	free(ps);
}

static struct drm_output_state *drm_pending_state_get_output(
				struct drm_pending_state *ps,
				struct drm_output *output)
{
	struct drm_output_state *output_state;

	list_for_each_entry(output_state, &ps->output_states, link) {
		if (output_state->output == output)
			return output_state;
	}

	return NULL;
}

static void *drm_repaint_begin(struct clv_compositor *c)
{
	struct drm_backend *b = to_drm_backend(c);
	struct drm_pending_state *ps;

	ps = drm_pending_state_alloc(b);
	drm_debug("start pending_state %p", ps);
	b->repaint_data = ps;
	return ps;
}

static void drm_repaint_cancel(struct clv_compositor *c, void *repaint_data)
{
	struct drm_backend *b = to_drm_backend(c);
	struct drm_pending_state *ps = repaint_data;

	drm_debug("cancel pending_state %p", ps);
	drm_pending_state_free(ps);
	b->repaint_data = NULL;
}

static void drm_repaint_flush(struct clv_compositor *c, void *repaint_data)
{
	struct drm_backend *b = to_drm_backend(c);
	struct drm_pending_state *ps = repaint_data;

	drm_debug("flush pending_state %p", ps);
	drm_pending_state_apply(ps);
	b->repaint_data = NULL;
}

static s32 get_prop_value(s32 fd, drmModeObjectProperties *props,
			  const char *name, u32 *value)
{
	drmModePropertyPtr property;
	u32 i;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!property)
			continue;
		if (!strcasecmp(property->name, name)) {
			*value = props->prop_values[i];
			drmModeFreeProperty(property);
			return 0;
		}
		drmModeFreeProperty(property);
	}
	return -ENOENT;
}

static s32 get_prop_id(s32 fd, drmModeObjectProperties *props, const char *name)
{
	drmModePropertyPtr property;
	u32 i, id = 0;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!property)
			continue;
		if (!strcasecmp(property->name, name))
			id = property->prop_id;
		drmModeFreeProperty(property);
		if (id)
			return id;
	}

	return -1;
}

static s32 drm_mode_ensure_blob(struct drm_backend *b, struct drm_mode *mode)
{
	s32 ret;

	if (mode->blob_id)
		return 0;

	ret = drmModeCreatePropertyBlob(b->fd,
					&mode->mode_info,
					sizeof(mode->mode_info),
					&mode->blob_id);
	if (ret != 0)
		drm_err("failed to create mode property blob: %s",
			strerror(errno));

	drm_debug("\t\t\t[atomic] created new mode blob %u for %s",
		  mode->blob_id, mode->mode_info.name);

	return ret;
}

static u32 drm_refresh_rate_mhz(const drmModeModeInfo *info)
{
	u64 refresh;

	/* Calculate higher precision (mHz) refresh rate */
	refresh = (info->clock * 1000000LL / info->htotal +
		   info->vtotal / 2) / info->vtotal;

	if (info->flags & DRM_MODE_FLAG_INTERLACE)
		refresh *= 2;
	if (info->flags & DRM_MODE_FLAG_DBLSCAN)
		refresh /= 2;
	if (info->vscan > 1)
	    refresh /= info->vscan;

	return refresh;
}

static void drm_output_print_modes(struct drm_output *output)
{
	struct clv_mode *m;
	struct drm_mode *mode;

	list_for_each_entry(m, &output->base.modes, link) {
		mode = container_of(m, struct drm_mode, base);
		drm_info("%ux%u@%.1f%s, %.1f MHz",
			 m->w, m->h, m->refresh / 1000.0f,
			 m->flags & MODE_PREFERRED ? ", Preferred" : "",
			 mode->mode_info.clock / 1000.0);
	}
}

static struct drm_mode *drm_mode_create(const drmModeModeInfo *info)
{
	struct drm_mode *mode;

	mode = malloc(sizeof(*mode));
	if (mode == NULL)
		return NULL;

	mode->base.flags = 0;
	mode->base.w = info->hdisplay;
	mode->base.h = info->vdisplay;
	mode->base.refresh = drm_refresh_rate_mhz(info);
	mode->mode_info = *info;
	mode->blob_id = 0;

	if (info->type & DRM_MODE_TYPE_PREFERRED)
		mode->base.flags |= MODE_PREFERRED;

	INIT_LIST_HEAD(&mode->base.link);
	return mode;
}

static s32 drm_output_add_modes(struct drm_output *output)
{
	s32 count = 0, i;
	struct drm_mode *mode;
	struct drm_head *head = output->head;
	
	assert(head);
	if (head->connector->connection != DRM_MODE_CONNECTED)
		return 0;

	for (i = 0; i < head->connector->count_modes; i++) {
		if (head->connector->modes[i].flags & DRM_MODE_FLAG_INTERLACE)
			continue;
		mode = drm_mode_create(&head->connector->modes[i]);
		if (!mode)
			continue;
		list_add_tail(&mode->base.link, &output->base.modes);
		count++;
	}

	return count;
}

static void drm_mode_destroy(struct drm_backend *b, struct drm_mode *mode)
{
	if (mode->blob_id)
		drmModeDestroyPropertyBlob(b->fd, mode->blob_id);

	list_del(&mode->base.link);
	free(mode);
}

static void drm_output_clear_modes(struct drm_output *output)
{
	struct clv_mode *mode, *next;
	struct drm_mode *dmode;

	list_for_each_entry_safe(mode, next, &output->base.modes, link) {
		dmode = to_drm_mode(mode);
		drm_mode_destroy(output->b, dmode);
	}
}

static void drm_head_retrieve_modes(struct clv_head *head)
{
	struct drm_head *h = to_drm_head(head);

	drm_output_clear_modes(h->output);
	if (h->connector->connection == DRM_MODE_CONNECTED) {
		drm_output_add_modes(h->output);
		drm_output_print_modes(h->output);
		head->connected = 1;
	} else {
		head->connected = 0;
	}
}

/* Determine the type of vblank synchronization to use for the output.
 *
 * The pipe parameter indicates which CRTC is in use.  Knowing this, we
 * can determine which vblank sequence type to use for it.  Traditional
 * cards had only two CRTCs, with CRTC 0 using no special flags, and
 * CRTC 1 using DRM_VBLANK_SECONDARY.  The first bit of the pipe
 * parameter indicates this.
 *
 * Bits 1-5 of the pipe parameter are 5 bit wide pipe number between
 * 0-31.  If this is non-zero it indicates we're dealing with a
 * multi-gpu situation and we need to calculate the vblank sync
 * using DRM_BLANK_HIGH_CRTC_MASK.
 */
static u32 drm_waitvblank_pipe(struct drm_output *output)
{
	if (output->index > 1)
		return (output->index << DRM_VBLANK_HIGH_CRTC_SHIFT)
			& DRM_VBLANK_HIGH_CRTC_MASK;
	else if (output->index > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static void drm_output_start_repaint_loop(struct clv_output *base)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_plane *primary_plane = output->primary_plane;
	struct drm_backend *b = output->b;
	struct timespec now, ts, vbl2now;
	drmVBlank vbl = {
		.request.type = DRM_VBLANK_RELATIVE,
		.request.sequence = 0,
		.request.signal = 0,
	};
	s64 refresh_nsec;
	s32 ret;

	drm_debug("start repaint loop");
	
	if (output->disable_pending) {
		drm_debug("disable pending, return from repaint loop.");
		return;
	}

	if (!output->primary_plane->state_cur->fb)
		goto finish_frame;

	if (b->state_invalid)
		goto finish_frame;

	assert(primary_plane->state_cur->output == output);

	/* Try to get current timestamp via instant query */
	vbl.request.type |= drm_waitvblank_pipe(output);
	ret = drmWaitVBlank(b->fd, &vbl);

	/* Error ret or zero timestamp means failure to get valid timestamp */
	if ((ret == 0) && (vbl.reply.tval_sec > 0 || vbl.reply.tval_usec > 0)) {
		ts.tv_sec = vbl.reply.tval_sec;
		ts.tv_nsec = vbl.reply.tval_usec * 1000;

		clock_gettime(b->c->clk_id, &now);
		timespec_sub(&vbl2now, &now, &ts);
		refresh_nsec =
			millihz_to_nsec(output->base.current_mode->refresh);
		drm_debug("vbl2now: %ld refresh_nsec: %ld",
			  timespec_to_nsec(&vbl2now), refresh_nsec);
		//if (timespec_to_nsec(&vbl2now) < refresh_nsec) {
			clv_output_finish_frame(base, &ts);
			return;
		//}
	}


	drm_debug("ret = %d vbl.replay %lu %lu %d %d", ret, vbl.reply.tval_sec,
		  vbl.reply.tval_usec,
		  vbl.reply.tval_sec > 0, vbl.reply.tval_usec > 0);
//	drm_debug("%lu %lu", timespec_to_nsec(&vbl2now) / 1000000l,
//		  refresh_nsec / 1000000l);


//	assert(0);
	clv_output_finish_frame(base, NULL);

	return;

finish_frame:
	clv_output_finish_frame(base, NULL);
}

static struct drm_plane_state *drm_output_state_get_plane(
					struct drm_output_state *output_state,
					struct drm_plane *plane);

static struct drm_plane_state *drm_plane_state_alloc(
					struct drm_plane *plane,
					struct drm_output_state *state_output);

static void drm_plane_state_put_back(struct drm_plane_state *state)
{
	struct drm_output_state *state_output;
	struct drm_plane *plane;

	ps_debug("plane state put back");
	if (!state)
		return;

	state_output = state->output_state;
	plane = state->plane;
	clv_debug("call plane state free");
	drm_plane_state_free(state, 0);

	if (!plane->state_cur->fb)
		return;

	(void)drm_plane_state_alloc(plane, state_output);
}

static struct drm_fb *drm_output_render_gl(struct drm_output_state *state)
{
	struct drm_output *output = state->output;
	struct drm_backend *b = to_drm_backend(output->base.c);
	struct gbm_bo *bo;
	struct drm_fb *ret;
	//struct timespec t1, t2;

	drm_debug("render gl: %u %d,%d %ux%u",
		  output->index,
		  output->base.render_area.pos.x,
		  output->base.render_area.pos.y,
		  output->base.render_area.w,
		  output->base.render_area.h);
	//clock_gettime(b->c->clk_id, &t1);
	output->base.c->renderer->repaint_output(&output->base);
	//clock_gettime(b->c->clk_id, &t2);
	//printf("renderer repaint %u spent %ld ms\n", output->index,
	//	    timespec_sub_to_msec(&t2, &t1));

	bo = gbm_surface_lock_front_buffer(output->gbm_surface);
	if (!bo) {
		drm_err("failed to lock front buffer: %s", strerror(errno));
		return NULL;
	}

	ret = drm_fb_get_from_bo(bo, b, 1, DRM_BUF_GBM_SURFACE);
	if (!ret) {
		drm_err("failed to get drm_fb for bo");
		gbm_surface_release_buffer(output->gbm_surface, bo);
		return NULL;
	}
	ret->gbm_surface = output->gbm_surface;

	return ret;
}

static void drm_output_render(struct drm_output_state *state)
{
	struct drm_output *output = state->output;
	struct drm_plane_state *primary_state;
	struct drm_plane *primary_plane = output->primary_plane;
	struct drm_fb *fb;

	drm_debug("output render");
	primary_state = drm_output_state_get_plane(state,
						   output->primary_plane);
	if (primary_state->fb) {
		assert(0);
		return;
	}

	if (!output->base.primary_dirty && primary_plane->state_cur->fb &&
	    primary_plane->state_cur->fb->type == DRM_BUF_GBM_SURFACE &&
	    primary_plane->state_cur->fb->w ==
		output->base.current_mode->w &&
	    primary_plane->state_cur->fb->h ==
		output->base.current_mode->h) {
		drm_debug("TAG");
		fb = drm_fb_ref(primary_plane->state_cur->fb);
	} else {
		drm_debug("TAG");
		fb = drm_output_render_gl(state);
/*
		if (output->base.changed) {
			output->base.changed--;
		}
*/
		output->base.primary_dirty = 0;
	}

	if (!fb) {
		//assert(0);
		drm_plane_state_put_back(primary_state);
		return;
	}

	primary_state->fb = fb;
	primary_state->output = output;

	primary_state->src_x = 0;
	primary_state->src_y = 0;
	primary_state->src_w = output->base.current_mode->w << 16;
	primary_state->src_h = output->base.current_mode->h << 16;

	primary_state->crtc_x = 0;
	primary_state->crtc_y = 0;
	primary_state->crtc_w = primary_state->src_w >> 16;
	primary_state->crtc_h = primary_state->src_h >> 16;
}

static s32 drm_output_repaint(struct clv_output *base, void *repaint_data)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_output_state *state;
	struct drm_pending_state *ps = repaint_data;
	struct drm_plane_state *primary_state;

	if (output->disable_pending) {
		drm_debug("disable pending, return from output repaint.");
		goto error;
	}

	drm_debug("output repaint %u, enabled ? %d", base->index,
		  base->enabled);
	assert(!output->state_last);
	state = drm_pending_state_get_output(ps, output);
	drm_debug("state = %p", state);
	if (!state) {
		state = drm_output_state_dup(output->state_cur, ps, 1);
	}
	assert(state);
	state->dpms = CLV_DPMS_ON;
	drm_output_render(state);
	primary_state = drm_output_state_get_plane(state,output->primary_plane);
	if (!primary_state) {
		drm_err("cannot get primary_state");
		goto error;
	}
	
	return 0;

error:
	drm_output_state_free(state);
	return -1;
}

static struct drm_plane_state *drm_output_state_get_existing_plane(
				struct drm_output_state *output_state,
				struct drm_plane *plane);

static void cursor_update(struct drm_backend *b, struct clv_view *v)
{
	struct drm_output *output;
	struct gbm_bo *bo;
	struct clv_buffer *buffer = v->cursor_buf;
	struct shm_buffer *shm_buf = container_of(buffer, struct shm_buffer,
						  base);

	list_for_each_entry(output, &b->outputs, link) {
		drm_debug("update output[%d]'s cursor", output->index);
		output->cursor_index = 1 - output->cursor_index;
		bo = output->cursor_fb[output->cursor_index]->bo;
		if (gbm_bo_write(bo, shm_buf->shm.map,
				buffer->stride * buffer->h) < 0)
			gbm_err("write cursor bo failed. %s", strerror(errno));
		clv_region_fini(&v->surface->damage);
		clv_region_init(&v->surface->damage);
	}
}

#ifdef CONFIG_ROCKCHIP_DRM_HWC
static void cursor_relocate(struct drm_output *output, struct clv_view *v,
			    s32 x, s32 y, u32 width, u32 height)
{
	struct gbm_bo *bo;
	struct clv_buffer *buffer = v->cursor_buf;
	struct shm_buffer *shm_buf = container_of(buffer, struct shm_buffer,
						  base);
	u32 buf[buffer->w * buffer->h];
	u32 cp_w, cp_h, skip_x, skip_y;
	s32 i;

	bo = output->cursor_fb[output->cursor_index]->bo;

	if (x > 0) {
		skip_x = 0;
		cp_w = width;
	} else {
		skip_x = -x;
		cp_w = width + x;
	}

	if (y > 0) {
		skip_y = 0;
		cp_h = height;
	} else {
		skip_y = -y;
		cp_h = height + y;
	}

	memset(buf, 0, buffer->w * buffer->h * 4);
	for (i = 0; i < cp_h; i++) {
		memcpy(buf + i * buffer->w,
		       shm_buf->shm.map + (i + skip_y) * buffer->stride
		       		+ skip_x * 4,
		       cp_w * 4);
	}

	if (gbm_bo_write(bo, buf, sizeof(buf)) < 0)
		gbm_err("write cursor bo failed. %s", strerror(errno));
}
#endif

static struct drm_plane_state * drm_output_prepare_cursor_view(
					struct drm_output_state *output_state,
					struct clv_view *v)
{
	struct drm_plane_state *state = NULL;
	struct drm_output *output = output_state->output;
	struct clv_compositor *c = output->base.c;
	struct drm_backend *b = to_drm_backend(c);
	s32 calc, left, top, x, y, offs_x, offs_y, crtc_w, crtc_h;
	s32 need_update = 0;
	u32 width, height;

	if (!v->cursor_buf)
		return NULL;

	if (clv_region_is_not_empty(&v->surface->damage))
		need_update = 1;

	if (need_update) {
		cursor_update(output->b, v);

		if (v->need_to_draw) {
			v->need_to_draw--;
		}
		v->painted = 1;
	}

	/*
	 * cursor cannot be scaled.
	 *     (the cursor layer's size is limited to b->cursor_width and
	 *      b->cursor_height, scaling might cause cursur's size be out of
	 *      range! )
	 */

	if ((v->area.pos.x + (s32)(v->surface->w))
	       <= output->base.render_area.pos.x
	    || (v->area.pos.y + (s32)(v->surface->h))
	       <= output->base.render_area.pos.y
	    || v->area.pos.x >= (s32)(output->base.render_area.pos.x
	        + (s32)(output->base.render_area.w))
	    || v->area.pos.y >= (s32)(output->base.render_area.pos.y
	        + (s32)(output->base.render_area.h))) {
		drm_debug("not in output's render area %d,%d",
			  v->area.pos.x, v->area.pos.y);
		drm_debug("surface %ux%u", v->surface->w, v->surface->h);
		drm_debug("output area %d,%d %ux%u",
			  output->base.render_area.pos.x,
			  output->base.render_area.pos.y,
			  output->base.render_area.w,
			  output->base.render_area.h);
		state = drm_output_state_get_existing_plane(output_state,
						output->cursor_plane);
		if (state) {
			state->fb = NULL;
		}

		return NULL;
	}

	ps_debug("prepare cursor view's state");
	v->plane = &output->cursor_plane->base;

	state = drm_output_state_get_plane(output_state, output->cursor_plane);
	if (state && state->fb) {
		assert(0);
		return NULL;
	}

	state->output = output;

	calc = output->base.current_mode->w * output->base.render_area.h
		/ output->base.render_area.w;
	if (calc <= output->base.current_mode->h) {
		left = 0;
		top = (output->base.current_mode->h - calc) / 2;
		crtc_w = output->base.current_mode->w;
		crtc_h = calc;
	} else {
		calc = output->base.render_area.w * output->base.current_mode->h
			/ output->base.render_area.h;
		left = (output->base.current_mode->w - calc) / 2;
		top = 0;
		crtc_w = calc;
		crtc_h = output->base.current_mode->h;
	}

	offs_x = (v->area.pos.x - output->base.render_area.pos.x) * crtc_w
			/ (s32)output->base.render_area.w;
	offs_y = (v->area.pos.y - output->base.render_area.pos.y) * crtc_h
			/ (s32)output->base.render_area.h;
	drm_debug("offs (%d,%d) view (%d,%d) render_base (%d, %d)",
		  offs_x, offs_y, v->area.pos.x, v->area.pos.y,
		  output->base.render_area.pos.x,
		  output->base.render_area.pos.y);

	if (offs_x <= (-(s32)(b->cursor_width)))
		offs_x = (-(s32)(b->cursor_width)) + 1;

	if (offs_y <= (-(s32)(b->cursor_height)))
		offs_y = (-(s32)(b->cursor_height)) + 1;

	x = left + offs_x - (MIN(v->hot_x, b->cursor_width));
	y = top + offs_y - (MIN(v->hot_y, b->cursor_height));
	width = (crtc_w - (offs_x - (MIN(v->hot_x, b->cursor_width))))
			> v->surface->w ? v->surface->w
			: (crtc_w - offs_x + (MIN(v->hot_x, b->cursor_width)));
	height = (crtc_h - (offs_y - (MIN(v->hot_y, b->cursor_height))))
			> v->surface->h ? v->surface->h
			: (crtc_h - offs_y + (MIN(v->hot_y, b->cursor_height)));
	drm_debug("crtc_w %u offs_x %d (MIN(v->hot_x, b->cursor_width) %d "
		  "surface w %u crtc_w - offs x %u",
		  crtc_w, offs_x, MIN(v->hot_x, b->cursor_width),
			v->surface->w, crtc_w - offs_x);
	drm_debug("============ (%d - %d) * %u / %u = %d ===========",
		v->area.pos.x, output->base.render_area.pos.x,
		crtc_w, output->base.render_area.w, offs_x);
	drm_debug("============ (%d - %d) * %u / %u = %d ===========",
		v->area.pos.y, output->base.render_area.pos.y,
		crtc_h, output->base.render_area.h, offs_y);
	drm_debug("width = %u height = %u", width, height);

	state->v = v;
	state->fb = drm_fb_ref(output->cursor_fb[output->cursor_index]);
	//printf("Use Cursor bo %d, fb %u\n", output->cursor_index,
	//	state->fb->fb_id);

	state->src_x = 0;
	state->src_y = 0;
	state->src_w = width << 16;
	state->src_h = height << 16;

	state->crtc_x = x;
	state->crtc_y = y;
	state->crtc_w = width;
	state->crtc_h = height;

#ifdef CONFIG_ROCKCHIP_DRM_HWC
	if (x < 0 || y < 0) {
		if (state->output) {
			if (x < 0)
				state->crtc_x = 0;
			if (y < 0)
				state->crtc_y = 0;
			state->output->cursor_relocate = 1;
			cursor_relocate(output, v, x, y, width, height);
		}
	} else {
		if (state->output && state->output->cursor_relocate) {
			state->output->cursor_relocate = 0;
			cursor_relocate(output, v, x, y, width, height);
		}
	}
#endif

	drm_debug("output %d's cursor pos: %d,%d %ux%u", output->index,
		  state->crtc_x, state->crtc_y, state->crtc_w, state->crtc_h);

	return state;
}

static struct drm_plane_state * drm_output_prepare_overlay_view(
					struct drm_output_state *output_state,
					struct clv_view *v)
{
	struct drm_plane_state *state = NULL;
	struct drm_plane *plane;
	struct drm_output *output = output_state->output;
	u32 width, height;
	s32 calc, left, top;

	ps_debug("prepare overylay view's state");
	if (!v->curr_dmafb) {
		ps_warn("view %p's curr_dmafb == %p", v, v->curr_dmafb);
		return NULL;
	}

	ps_debug("ref %p", v->curr_dmafb);
	drm_fb_ref(v->curr_dmafb);
	plane = output->overlay_plane;

	state = drm_output_state_get_plane(output_state, plane);
	if (state->fb) {
		assert(0);
		return NULL;
	}
	state->fb = v->curr_dmafb;
//	clv_debug("state->fb->dma_fd = %d", state->fb->dma_fd);

	calc = output->base.current_mode->w * output->base.render_area.h
		/ output->base.render_area.w;
	if (calc <= output->base.current_mode->h) {
		left = 0;
		top = (output->base.current_mode->h - calc) / 2;
		width = output->base.current_mode->w;
		height = calc;
	} else {
		calc = output->base.render_area.w * output->base.current_mode->h
			/ output->base.render_area.h;
		left = (output->base.current_mode->w - calc) / 2;
		top = 0;
		width = calc;
		height = output->base.current_mode->h;
	}
	drm_debug("Plane view port(%d,%d %ux%u)", left, top, width, height);
	state->crtc_x = left;
	state->crtc_y = top;
	state->crtc_w = width;
	state->crtc_h = height;
	state->src_x = (v->area.pos.x - output->base.render_area.pos.x) << 16;
	state->src_y = (v->area.pos.y - output->base.render_area.pos.y) << 16;
	state->src_w = v->area.w << 16;
	state->src_h = v->area.h << 16;
	if ((state->src_x >> 16) > output->base.render_area.w
				- (state->src_w >> 16)) {
		drm_warn("src_x too large %u", state->src_x >> 16);
		state->src_x = (output->base.render_area.w
				- (state->src_w >> 16)) << 16;
	}
	if ((state->src_y >> 16) > output->base.render_area.h
				- (state->src_h >> 16)) {
		drm_warn("src_y too large %u", state->src_y >> 16);
		state->src_y = (output->base.render_area.h
				- (state->src_h >> 16)) << 16;
	}
	state->v = v;
	v->plane = &output->overlay_plane->base;
	drm_debug("view(%p)'s area: %d,%d %ux%u", v,
		  v->area.pos.x, v->area.pos.y,
		  v->area.w, v->area.h);
	drm_debug("output's area: %d,%d %ux%u", output->base.render_area.pos.x,
		  output->base.render_area.pos.y,
		  output->base.render_area.w,
		  output->base.render_area.h);
	drm_debug("[%u] %d,%d %ux%u -> %d,%d %ux%u", output->index,
		  state->src_x >> 16, state->src_y >> 16,
		  state->src_w >> 16, state->src_h >> 16,
		  state->crtc_x, state->crtc_y,
		  state->crtc_w, state->crtc_h);
	drm_debug("state->v = %p need_to_draw: %u", v, v->need_to_draw);

	return state;
}

static void drm_output_assign_planes(struct clv_output *output_base,
				     void *repaint_data)
{
	struct drm_output *output = to_drm_output(output_base);
	struct clv_compositor *c = output_base->c;
	struct clv_view *view;
	struct drm_output_state *state = NULL;
	struct drm_pending_state *ps = repaint_data;

	drm_debug("assign planes");
	list_for_each_entry(view, &c->views, link) {
		if (view->type == CLV_VIEW_TYPE_PRIMARY) {
			drm_debug("View %p is primary view", view);
			if (view->output_mask & (1 << output->index)) {
				view->plane = &c->primary_plane;
			}
		} else if (view->type == CLV_VIEW_TYPE_OVERLAY) {
			if (!(view->output_mask & (1 << output->index)))
				continue;
			drm_debug("View %p is overlay view", view);
			state = drm_pending_state_get_output(ps, output);
			if (!state) {
				state = drm_output_state_dup(output->state_cur,
							     ps, 1);
			}
			drm_output_prepare_overlay_view(state, view);
		} else if (view->type == CLV_VIEW_TYPE_CURSOR) {
			drm_debug("TAG");
			if (!(view->output_mask & (1 << output->index)))
				continue;
			drm_debug("View %p is cursor view", view);
			state = drm_pending_state_get_output(ps, output);
			if (!state) {
				state = drm_output_state_dup(output->state_cur,
							     ps, 1);
			}
			drm_output_prepare_cursor_view(state, view);
		} else {
			drm_err("View %p's type is unknown.", view);
		}
	}
}

static void drm_output_cursor_bo_destroy(struct drm_output *output)
{
	u32 i;

	drm_debug("destroy cursor fb");
	for (i = 0; i < ARRAY_SIZE(output->cursor_fb); i++) {
		drm_fb_unref(output->cursor_fb[i]);
		output->cursor_fb[i] = NULL;
	}
}

static void drm_output_fini_egl(struct drm_output *output)
{
	if (output->primary_plane->state_cur->fb
	  && output->primary_plane->state_cur->fb->type == DRM_BUF_GBM_SURFACE){
		drm_plane_state_free(output->primary_plane->state_cur, 1);
		output->primary_plane->state_cur = drm_plane_state_alloc(
						output->primary_plane, NULL);
		output->primary_plane->state_cur->complete = 1;
	}

	if (output->overlay_plane && output->overlay_plane->state_cur) {
		drm_debug("free overlay plane state");
		if (output->overlay_plane->state_cur->fb) {
			drm_plane_state_free(output->overlay_plane->state_cur,
					     1);
			output->overlay_plane->state_cur
				= drm_plane_state_alloc(output->overlay_plane,
							NULL);
			output->overlay_plane->state_cur->complete = 1;
		}
	}

	gl_renderer->output_destroy(&output->base);
	if (output->gbm_surface) {
		gbm_surface_destroy(output->gbm_surface);
		output->gbm_surface = NULL;
	}
}

static s32 drm_output_cursor_bo_create(struct drm_output *output,
				       struct drm_backend *b)
{
	u32 i;
	struct gbm_bo *bo;

	drm_debug("create cursor fb");
	if (!output->cursor_plane) {
		drm_warn("no cursor plane");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(output->cursor_fb); i++) {
		drm_debug("create cursor fb %ux%u", b->cursor_width,
			  b->cursor_height);
		bo = gbm_bo_create(b->gbm, b->cursor_width, b->cursor_height,
				   GBM_FORMAT_ARGB8888,
				   GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
		if (!bo)
			goto err;

		output->cursor_fb[i] =
			drm_fb_get_from_bo(bo, b, 0, DRM_BUF_CURSOR);
		if (!output->cursor_fb[i]) {
			gbm_bo_destroy(bo);
			goto err;
		}
	}

	return 0;

err:
	drm_err("cursor buffers unavailable, using gl cursors\n");
	drm_output_cursor_bo_destroy(output);
	return -1;
}

static s32 drm_output_init_egl(struct drm_output *output)
{
	struct clv_mode *mode = output->base.current_mode;
	struct drm_backend *b = output->b;
	s32 vid;

	drm_debug("current_mode %u x %u",
		  output->base.current_mode->w,
		  output->base.current_mode->h);
	output->gbm_surface = gbm_surface_create(b->gbm, mode->w, mode->h,
						 b->gbm_format,
						 output->gbm_bo_flags);
	if (!output->gbm_surface) {
		drm_err("failed to create gbm surface %p %u %u %s",
			b->gbm, mode->w, mode->h, strerror(errno));
		return -1;
	}

	if (gl_renderer->output_create(&output->base, NULL, output->gbm_surface,
				       (s32 *)(&b->gbm_format), 1, &vid) < 0) {
		drm_err("failed to create renderer output");
		gbm_surface_destroy(output->gbm_surface);
		output->gbm_surface = NULL;
		return -1;
	}

	return 0;
}

static void drm_output_enable(struct clv_output *base,
			      struct clv_rect *render_area)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_pending_state *ps;

	drm_debug("Enabling CRTC %u...", output->crtc_id);
	if (output->disable_pending) {
		// TODO
		drm_output_fini_egl(output);
		ps = drm_pending_state_alloc(output->b);
		drm_output_get_disable_state(ps, output);
		drm_pending_state_apply_sync(ps);
		output->disable_pending = 0;
	}
	output->base.changed = 1;
	memcpy(&base->render_area, render_area, sizeof(struct clv_rect));
	drm_debug("CRTC render_area: %d,%d %ux%u",
		  base->render_area.pos.x, base->render_area.pos.y,
		  base->render_area.w, base->render_area.h);
	drm_output_init_egl(output);
#ifdef CLEAN_FLIP_LISTENER_WHEN_DISABLE
	/*
	 * reinit flip_signal list.
	 */
	clv_signal_init(&base->flip_signal);
#endif
	base->enabled = 1;
	drm_debug("CRTC %u is enabled.", output->crtc_id);
}

static s32 drm_output_disable(struct clv_output *base)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_pending_state *ps;
#ifdef CLEAN_FLIP_LISTENER_WHEN_DISABLE
	struct clv_listener *l, *next;
#endif

	if (!base->enabled && !output->disable_pending)
		return 0;
	drm_debug("Disabling CRTC %u...", output->crtc_id);
	base->enabled = 0;
	if (output->atomic_complete_pending) {
		drm_debug("page flip not complete, disable pending.");
		output->disable_pending = 1;
		return -1;
	}
	drm_output_fini_egl(output);
	//TODO
	ps = drm_pending_state_alloc(output->b);
	drm_output_get_disable_state(ps, output);
	drm_pending_state_apply_sync(ps);
	output->disable_pending = 0;
#ifdef CLEAN_FLIP_LISTENER_WHEN_DISABLE
	/*
	 * remove all page flip listener,
	 * to prevent there is invalid listener in the list.
	 */
	list_for_each_entry_safe(l, next,
				 &base->flip_signal.listener_list,
				link) {
		list_del(&l->link);
		INIT_LIST_HEAD(&l->link);
	}
#endif
	base->enabled = 0;
	drm_debug("CRTC %u is disabled.", output->crtc_id);
	return 0;
}

static void clv_plane_init(struct clv_compositor *c, struct clv_plane *plane,
			   u32 index, struct clv_output *output)
{
	struct clv_plane *p;

	plane->c = c;
	plane->index = index;
	plane->output = output;

	drm_debug("Plane stack debug:");
	clv_printf("TOP < ");
	list_for_each_entry(p, &c->planes, link) {
		clv_printf("%s %p ", p->name, p);
	}
	clv_printf("> Bottom\n");
}

static void clv_plane_fini(struct clv_plane *plane)
{
	list_del(&plane->link);
}

static void drm_plane_destroy(struct drm_plane *plane)
{
	drm_debug("Destroying DRM plane...");
	if (!plane)
		return;

	clv_plane_fini(&plane->base);

	plane->output = NULL;

	if (plane->props) {
		drmModeFreeObjectProperties(plane->props);
		plane->props = NULL;
	}

	if (plane->plane) {
		drmModeFreePlane(plane->plane);
		plane->plane = NULL;
	}

	list_del(&plane->link);

	free(plane);

	drm_debug("DRM plane destroyed.");
}

static struct drm_plane_state *drm_plane_state_alloc(
					struct drm_plane *plane,
					struct drm_output_state *state_output)
{
	struct drm_plane_state *state = calloc(1, sizeof(*state));

	assert(state);
	state->output_state = state_output;
	state->plane = plane;
	if (state_output)
		list_add_tail(&state->link, &state_output->plane_states);
	else
		INIT_LIST_HEAD(&state->link);
	ps_debug("plane state alloc for plane [%u] %p", plane->index,
		  state);
	return state;
}

static struct drm_plane_state *drm_plane_state_dup(
				struct drm_plane_state *src,
				struct drm_output_state *state_output)
{
	struct drm_plane_state *dst = calloc(1, sizeof(*dst));
	struct drm_plane_state *old, *tmp;

	assert(src);
	assert(dst);
	*dst = *src;
	INIT_LIST_HEAD(&dst->link);

	list_for_each_entry_safe(old, tmp, &state_output->plane_states, link) {
		assert(old != src);
		if (old->plane == dst->plane) {
			clv_debug("call plane state free");
			drm_plane_state_free(old, 0);
		}
	}

	list_add_tail(&state_output->plane_states, &dst->link);
	if (src->fb)
		dst->fb = drm_fb_ref(src->fb);
	dst->output_state = state_output;
	dst->complete = 0;

	return dst;
}

static struct drm_plane *drm_plane_create(struct clv_compositor *c, u32 index,
					  struct clv_output *output)
{
	struct drm_plane *plane;
	struct drm_backend *b = to_drm_backend(c);
	u32 type = 0;

	drm_debug("Creating DRM plane...");
	plane = calloc(1, sizeof(*plane));
	if (!plane)
		return NULL;

	plane->b = b;
	plane->index = index;
	plane->plane_id = b->pres->planes[plane->index];
	plane->plane = drmModeGetPlane(b->fd, plane->plane_id);
	if (!plane->plane) {
		drm_err("failed to get drmModePlanePtr %s",strerror(errno));
		goto error;
	}

	plane->props = drmModeObjectGetProperties(b->fd, plane->plane_id,
						  DRM_MODE_OBJECT_PLANE);
	if (!plane->props) {
		drm_err("failed to get plane's properties. %s",
			strerror(errno));
		goto error;
	}

	plane->prop_crtc_id = get_prop_id(b->fd, plane->props, "CRTC_ID");
	plane->prop_fb_id = get_prop_id(b->fd, plane->props, "FB_ID");
	plane->prop_crtc_x = get_prop_id(b->fd, plane->props, "CRTC_X");
	plane->prop_crtc_y = get_prop_id(b->fd, plane->props, "CRTC_Y");
	plane->prop_crtc_w = get_prop_id(b->fd, plane->props, "CRTC_W");
	plane->prop_crtc_h = get_prop_id(b->fd, plane->props, "CRTC_H");
	plane->prop_src_x = get_prop_id(b->fd, plane->props, "SRC_X");
	plane->prop_src_y = get_prop_id(b->fd, plane->props, "SRC_Y");
	plane->prop_src_w = get_prop_id(b->fd, plane->props, "SRC_W");
	plane->prop_src_h = get_prop_id(b->fd, plane->props, "SRC_H");
	plane->prop_color_space = get_prop_id(b->fd, plane->props,
						"COLOR_SPACE");

	get_prop_value(b->fd, plane->props, "TYPE", &type);
	plane->type = (enum drm_plane_type)type;
	switch (plane->type) {
	case DRM_OVERLAY_PL:
		drm_debug("plane id %u, type %s.", plane->plane_id, "OVERLAY");
		__list_add(&plane->base.link, c->primary_plane.link.prev,
			   &c->primary_plane.link);
		sprintf(plane->base.name, "O-%u", plane->index);
		break;
	case DRM_PRIMARY_PL:
		drm_debug("plane id %u, type %s.", plane->plane_id, "PRIMARY");
		__list_add(&plane->base.link, c->primary_plane.link.prev,
			   &c->primary_plane.link);
		sprintf(plane->base.name, "P-%u", plane->index);
		break;
	case DRM_CURSOR_PL:
		drm_debug("plane id %u, type %s.", plane->plane_id, "CURSOR");
		list_add(&plane->base.link, &c->planes);
		sprintf(plane->base.name, "C-%u", plane->index);
		break;
	default:
		drm_err("unknown plane.");
		goto error;
	}

	plane->state_cur = drm_plane_state_alloc(plane, NULL);
	plane->state_cur->complete = 1;
	list_add_tail(&plane->link, &b->planes);

	clv_plane_init(c, &plane->base, plane->index, output);

	drm_debug("DRM plane created.");

	return plane;

error:
	drm_plane_destroy(plane);
	return NULL;
}

static void drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_debug("Destroying DRM encoder...");
	if (!encoder)
		return;

	encoder->output = NULL;
	encoder->head = NULL;

	if (encoder->enc) {
		drmModeFreeEncoder(encoder->enc);
		encoder->enc = NULL;
	}

	free(encoder);
	drm_debug("DRM encoder destroyed.");
}

static struct drm_encoder *drm_encoder_create(struct clv_compositor *c,
					      u32 index)
{
	struct drm_encoder *encoder;
	struct drm_backend *b = to_drm_backend(c);

	drm_debug("Creating DRM encoder...");
	encoder = calloc(1, sizeof(*encoder));
	if (!encoder)
		return NULL;

	encoder->b = b;
	encoder->index = index;
	encoder->encoder_id = b->res->encoders[encoder->index];
	encoder->enc = drmModeGetEncoder(b->fd, encoder->encoder_id);
	if (!encoder->enc) {
		drm_err("failed to get drmModeEncoderPtr %s",strerror(errno));
		goto error;
	}

	drm_debug("DRM encoder created.");

	return encoder;

error:
	drm_encoder_destroy(encoder);
	return NULL;
}

static void clv_head_init(struct clv_compositor *c, struct clv_head *head,
			  u32 index, struct clv_output *output)
{
	head->c = c;
	head->output = output;
	head->connected = 0;
	head->index = index;
	head->changed = 1;
	list_add_tail(&head->link, &c->heads);
	head->retrieve_modes = drm_head_retrieve_modes;
}

static void clv_head_fini(struct clv_head *head)
{
	list_del(&head->link);
}

static void drm_head_destroy(struct drm_head *head)
{
	drm_debug("Destroying DRM head...");
	if (!head)
		return;

	clv_head_fini(&head->base);

	if (head->hpd_detect_source) {
		clv_event_source_remove(head->hpd_detect_source);
		head->hpd_detect_source = NULL;
	}

	head->output = NULL;
	head->encoder = NULL;

	if (head->props) {
		drmModeFreeObjectProperties(head->props);
		head->props = NULL;
	}

	if (head->connector) {
		drmModeFreeConnector(head->connector);
		head->connector = NULL;
	}

	list_del(&head->link);

	free(head);
	drm_debug("DRM head destroyed.");
}

static struct drm_head *drm_head_create(struct clv_compositor *c, u32 index,
					struct clv_output *output)
{
	struct drm_head *head;
	struct drm_backend *b = to_drm_backend(c);
	struct clv_event_loop *loop = clv_display_get_event_loop(c->display);

	drm_debug("Creating DRM head...");
	head = calloc(1, sizeof(*head));
	if (!head)
		return NULL;

	head->b = b;
	head->index = index;
	head->connector_id = b->res->connectors[head->index];
	head->connector = drmModeGetConnector(b->fd, head->connector_id);
	if (!head->connector) {
		drm_err("failed to get drmModeConnectorPtr %s",strerror(errno));
		goto error;
	}

	head->props = drmModeObjectGetProperties(b->fd, head->connector_id,
						 DRM_MODE_OBJECT_CONNECTOR);
	if (!head->props) {
		drm_err("failed to get connector's properties. %s",
			strerror(errno));
		goto error;
	}

	head->prop_crtc_id = get_prop_id(b->fd, head->props, "CRTC_ID");
	head->prop_hdmi_quant_range = get_prop_id(b->fd, head->props,
						  "hdmi_quant_range");

	head->hpd_detect_source = clv_event_loop_add_timer(loop,
							   hpd_detect_proc,
							   head);
	assert(head->hpd_detect_source);

	list_add_tail(&head->link, &b->heads);

	clv_head_init(c, &head->base, head->index, output);
	drm_debug("DRM head created.");

	return head;

error:
	drm_head_destroy(head);
	return NULL;
}

static void clv_output_fini(struct clv_output *output)
{
	list_del(&output->link);
}

static void drm_output_destroy(struct clv_output *base)
{
	struct drm_output *output;
	struct drm_plane *plane, *next;

	drm_debug("Destroying DRM output...");
	if (!base)
		return;

	output = to_drm_output(base);

	drm_output_cursor_bo_destroy(output);

	clv_output_fini(base);

	output->primary_plane = NULL;
	output->cursor_plane = NULL;
	list_for_each_entry_safe(plane, next, &output->planes, output_link) {
		list_del(&plane->output_link);
		drm_plane_destroy(plane);
	}

	if (output->head) {
		drm_head_destroy(output->head);
		output->head = NULL;
	}

	if (output->encoder) {
		drm_encoder_destroy(output->encoder);
		output->encoder = NULL;
	}

	if (output->props) {
		drmModeFreeObjectProperties(output->props);
		output->props = NULL;
	}

	if (output->crtc) {
		drmModeFreeCrtc(output->crtc);
		output->crtc = NULL;
	}

	list_del(&output->link);
	free(output);

	drm_debug("DRM output destroyed.");
}

static void clv_output_init(struct clv_compositor *c, struct clv_output *output,
			    u32 index, struct clv_head *head)
{
	output->c = c;
	output->head = head;
	output->index = index;
	memset(&output->render_area, 0, sizeof(struct clv_rect));
	output->current_mode = NULL;
	INIT_LIST_HEAD(&output->modes);
	list_add_tail(&output->link, &c->outputs);
	output->start_repaint_loop = drm_output_start_repaint_loop;
	output->repaint = drm_output_repaint;
	output->assign_planes = drm_output_assign_planes;
	output->destroy = drm_output_destroy;
	output->enable = drm_output_enable;
	output->disable = drm_output_disable;
	clv_signal_init(&output->flip_signal);
}

static struct drm_plane_state *drm_output_state_get_existing_plane(
				struct drm_output_state *output_state,
				struct drm_plane *plane)
{
	struct drm_plane_state *state;

	ps_debug("get existing plane[%u] state from output[%u] state",
		  plane->index, output_state->output->index);
	list_for_each_entry(state, &output_state->plane_states, link) {
		if (state->plane == plane)
			return state;
	}

	return NULL;
}

static struct drm_plane_state *drm_output_state_get_plane(
					struct drm_output_state *output_state,
					struct drm_plane *plane)
{
	struct drm_plane_state *state;

	ps_debug("get plane[%u] state from output[%u] state",
		  plane->index, output_state->output->index);
	state = drm_output_state_get_existing_plane(output_state, plane);
	if (state)
		return state;

	return drm_plane_state_alloc(plane, output_state);
}

static struct drm_output_state *drm_output_state_alloc(
		struct drm_output *output, struct drm_pending_state *ps)
{
	struct drm_output_state *state = calloc(1, sizeof(*state));

	ps_debug("------> alloc output state.. output->index = %u, %p",
		 output->index, ps);
	assert(state);
	state->output = output;
	state->dpms = CLV_DPMS_OFF;
	state->pending_state = ps;
	if (ps) {
		list_add_tail(&state->link, &ps->output_states);
	} else {
		INIT_LIST_HEAD(&state->link);
	}

	INIT_LIST_HEAD(&state->plane_states);
	if (ps) {
		struct drm_output_state *o;

		ps_debug("output in ps: %p", ps);
		list_for_each_entry(o, &ps->output_states, link)
			ps_debug("\t%u", o->output->index);
	}
	drm_debug("------ %p", state);
	return state;
}

static struct drm_output_state *drm_output_state_dup(
				struct drm_output_state *src,
				struct drm_pending_state *ps,
				s32 reset_plane)
{
	struct drm_output_state *dst = calloc(1, sizeof(*dst));
	struct drm_plane_state *state;

	ps_debug("------> dup output state.. output->index = %u, %p src: %p",
		 src->output->index, ps, src);
	ps_debug("src->pending_state = %p", src->pending_state);
	assert(dst);

	*dst = *src;

	if (ps) {
		struct drm_output_state *o;

		ps_debug("output in ps: %p", ps);
		list_for_each_entry(o, &ps->output_states, link)
			ps_debug("\t%u", o->output->index);
	}

	dst->pending_state = ps;
	if (ps) {
		list_add_tail(&dst->link, &ps->output_states);
	} else {
		INIT_LIST_HEAD(&dst->link);
	}

	INIT_LIST_HEAD(&dst->plane_states);

	list_for_each_entry(state, &src->plane_states, link) {
		if (!state->output)
			continue;

		if (reset_plane)
			(void)drm_plane_state_alloc(state->plane, dst);
		else
			(void)drm_plane_state_dup(state, dst);
	}

	ps_debug("src->pending_state = %p", src->pending_state);
	if (ps) {
		struct drm_output_state *o;
		struct drm_plane_state *pls;

		ps_debug("output in ps: %p", ps);
		list_for_each_entry(o, &ps->output_states, link) {
			ps_debug("\t%u", o->output->index);
			ps_debug("\tplane in output: %p", o);
			list_for_each_entry(pls, &o->plane_states, link) {
				ps_debug("\t\t%u", pls->plane->index);
			}
		}
	}
	ps_debug("------ %p", dst);
	return dst;
}

static struct clv_output *drm_output_create(struct clv_compositor *c,
					    struct clv_head_config *head_config)
{
	struct drm_output *output;
	struct drm_plane *plane;
	struct drm_backend *b = to_drm_backend(c);
	u32 encoder_index = head_config->encoder.index;
	s32 i;

	drm_debug("Creating DRM output...");
	output = calloc(1, sizeof(*output));
	if (!output) {
		drm_err("cannnot alloc drm output");
		return NULL;
	}

	output->b = b;
	output->index = head_config->encoder.output.index;
	output->crtc_id = b->res->crtcs[output->index];
	output->crtc = drmModeGetCrtc(b->fd, output->crtc_id);
	if (!output->crtc) {
		drm_err("failed to get drmModeCrtcPtr %s", strerror(errno));
		goto error;
	}

	output->props = drmModeObjectGetProperties(b->fd, output->crtc_id,
						   DRM_MODE_OBJECT_CRTC);
	if (!output->props) {
		drm_err("failed to get CRTC's properties. %s", strerror(errno));
		goto error;
	}

	output->prop_active = get_prop_id(b->fd, output->props, "ACTIVE");
	output->prop_mode_id = get_prop_id(b->fd, output->props, "MODE_ID");

	INIT_LIST_HEAD(&output->link);

	output->head = drm_head_create(c, head_config->index, &output->base);
	if (!output->head) {
		drm_err("failed to create drm head");
		goto error;
	}

	output->encoder = drm_encoder_create(c, encoder_index);
	if (!output->encoder) {
		drm_err("failed to create drm encoder.");
		goto error;
	}

	output->encoder->output = output;
	output->encoder->head = output->head;
	output->head->output = output;
	output->head->encoder = output->encoder;

	INIT_LIST_HEAD(&output->planes);
	for (i = 0; i < head_config->encoder.output.count_layers; i++) {
		plane = drm_plane_create(c,
				head_config->encoder.output.layers[i].index,
				&output->base);
		if (!plane) {
			drm_err("failed to create drm plane");
			goto error;
		}
		if (plane->type == DRM_PRIMARY_PL)
			output->primary_plane = plane;
		if (plane->type == DRM_CURSOR_PL)
			output->cursor_plane = plane;
		if (plane->type == DRM_OVERLAY_PL)
			output->overlay_plane = plane;
		plane->output = output;
		list_add_tail(&plane->output_link, &output->planes);
	}

	output->gbm_bo_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

	output->disable_pending = 0;
	output->atomic_complete_pending = 0;

	output->state_cur = drm_output_state_alloc(output, NULL);

	list_add_tail(&output->link, &b->outputs);

	clv_output_init(c, &output->base, output->index, &output->head->base);

	drm_output_cursor_bo_create(output, b);
	drm_debug("DRM output created.");

	return &output->base;

error:
	drm_output_destroy(&output->base);
	return NULL;
}

static void drm_backend_destroy(struct clv_compositor *c)
{
	struct drm_backend *b = to_drm_backend(c);

	if (b->udev_drm_source) {
		clv_event_source_remove(b->udev_drm_source);
		udev_monitor_unref(b->udev_monitor);
		b->udev_drm_source = NULL;
	}

	if (b->drm_source) {
		clv_event_source_remove(b->drm_source);
		b->drm_source = NULL;
	}

	if (b->udev) {
		udev_unref(b->udev);
		b->udev = NULL;
	}

	if (b->gbm) {
		gbm_device_destroy(b->gbm);
		b->gbm = NULL;
	}

	if (b->pres) {
		drmModeFreePlaneResources(b->pres);
		b->pres = NULL;
	}

	if (b->res) {
		drmModeFreeResources(b->res);
		b->res = NULL;
	}

	if (b->fd > 0) {
		close(b->fd);
		b->fd = 0;
	}

	clv_region_fini(&b->canvas);
	free(b);

	c->backend = NULL;
}

struct clv_backend *drm_backend_create(struct clv_compositor *c)
{
	struct drm_backend *b;
	char *dev_node;
	s32 vid;

	drm_debug("Creating DRM Backend...");
	b = calloc(1, sizeof(*b));
	if (!b)
		return NULL;

	c->backend = &b->base;

	b->state_invalid = 1;

	b->c = c;

	clv_region_init(&b->canvas);

	dev_node = getenv("CLOVER_DEV_NODE");
	if (dev_node)
		strcpy(b->dev_node, dev_node);
	else
		strcpy(b->dev_node, "/dev/dri/card0");
	
	b->fd = open(b->dev_node, O_RDWR | O_CLOEXEC, 0644);
	if (b->fd < 0) {
		drm_err("failed to open %s. %s", b->dev_node,
			strerror(errno));
		goto error;
	}

	if (set_drm_caps(b) < 0) {
		drm_err("failed to set DRM capabilities");
		goto error;
	}

	b->res = drmModeGetResources(b->fd);
	if (!b->res) {
		drm_err("failed to get drmModeResource");
		goto error;
	}

	b->pres = drmModeGetPlaneResources(b->fd);
	if (!b->pres) {
		drm_err("failed to get drmModePlaneResource");
		goto error;
	}

	b->gbm_format = GBM_FORMAT_XRGB8888;
	b->gbm = gbm_create_device(b->fd);
	if (!b->gbm) {
		gbm_err("failed create gbm device.");
		goto error;
	}

	b->loop = clv_display_get_event_loop(c->display);

	b->udev = udev_new();
	if (!b->udev)
		goto error;

	if (get_drm_dev_sysnum(b->udev, b->dev_node, &b->sysnum) < 0)
		goto error;

	b->udev_monitor = udev_monitor_new_from_netlink(b->udev, "udev");
	if (!b->udev_monitor)
		goto error;
	udev_monitor_filter_add_match_subsystem_devtype(b->udev_monitor,
							"drm", NULL);
	udev_monitor_enable_receiving(b->udev_monitor);

	b->udev_drm_source = clv_event_loop_add_fd(b->loop,
						   udev_monitor_get_fd(
						       b->udev_monitor),
						   CLV_EVT_READABLE,
						   udev_drm_event_proc, b);
	if (!b->udev_drm_source)
		goto error;

	b->drm_source = clv_event_loop_add_fd(b->loop, b->fd,
					      CLV_EVT_READABLE,
					      drm_event_proc, b);
	if (!b->drm_source)
		goto error;

	INIT_LIST_HEAD(&b->outputs);
	INIT_LIST_HEAD(&b->planes);
	INIT_LIST_HEAD(&b->heads);

	b->base.destroy = drm_backend_destroy;
	b->base.repaint_begin = drm_repaint_begin;
	b->base.repaint_flush = drm_repaint_flush;
	b->base.repaint_cancel = drm_repaint_cancel;
	b->base.output_create = drm_output_create;
	b->base.dmabuf_destroy = drm_dmabuf_destroy;
	b->base.import_dmabuf = drm_import_dmabuf;

	renderer_create(c, (s32 *)&b->gbm_format, 1, 1, b->gbm, &vid);
	set_renderer_dbg(0);
	assert(c->renderer);
	gl_renderer = c->renderer;
	drm_debug("DRM Backend created.");

	drm_dbg = 0;
	gbm_dbg = 0;
	timer_dbg = 0;
	ps_dbg = 0;

	return &b->base;

error:
	drm_backend_destroy(c);
	return NULL;
}

void drm_set_dbg(u32 flags)
{
	drm_dbg = flags & 0x0F;
	gbm_dbg = (flags >> 4) & 0x0F;
	ps_dbg = (flags >> 8) & 0x0F;
	timer_dbg = (flags >> 12) & 0x0F;
}

