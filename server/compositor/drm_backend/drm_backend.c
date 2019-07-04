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
#include <drm_fourcc.h>
#include <drm/drm.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <clover_utils.h>
#include <clover_event.h>
#include <clover_log.h>
#include <clover_region.h>
#include <clover_compositor.h>

static u8 drm_dbg = 16;
static u8 gbm_dbg = 16;

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
	struct list_head link;
	enum clv_dpms dpms;
	struct list_head plane_states;
};

struct drm_plane_state {
	struct drm_plane *plane;
	struct drm_output *output;
	struct drm_output_state *output_state;

	struct drm_fb *fb;

	struct clv_view *v; /**< maintained for drm_assign_planes only */

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
	u32 encoder_id;
	u32 index;
	drmModeEncoderPtr enc;

	struct drm_backend *b;

	struct drm_output *output;
	struct drm_head *head;
};

struct drm_plane {
	struct clv_plane base;
	struct drm_backend *b;
	enum drm_plane_type type;
	struct list_head link; /* link to output->overlay_planes */
	struct list_head link_b;

	u32 index;
	u32 plane_id;

	struct drm_output *output;

	drmModePlanePtr pl;

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

	/* The last state submitted to the kernel for this plane. */
	struct drm_plane_state *state_cur;
};

struct drm_head {
	struct clv_head base;
	struct drm_backend *b;
	struct list_head link;

	struct drm_output *output;
	struct drm_encoder *encoder;

	drmModeConnectorPtr connector;
	u32 connector_id;

	u32 index;

	drmModeObjectProperties *props;
	u32 prop_crtc_id;
	u32 prop_dpms;
};

struct drm_output {
	struct clv_output base;
	struct drm_backend *b;

	struct list_head link;

	struct drm_head *head;
	struct drm_encoder *encoder;

	u32 index;

	u32 crtc_id;
	drmModeCrtcPtr crtc;

	s32 atomic_complete_pending;
	s32 disable_pending;
	s32 dpms_off_pending;

	struct drm_fb *gbm_cursor_fb[2];
	struct drm_plane *cursor_plane;
	struct clv_view *cursor_view;
	s32 current_cursor;

	struct gbm_surface *gbm_surface;
	uint32_t gbm_bo_flags;

	struct drm_plane *primary_plane;
	struct list_head overlay_planes;

	/* The last state submitted to the kernel for this CRTC. */
	struct drm_output_state *state_cur;
	/* The previously-submitted state, where the hardware has not
	 * yet acknowledged completion of state_cur. */
	struct drm_output_state *state_last;

	drmModeObjectProperties *props;

	u32 prop_active;
	u32 prop_mode_id;
	u32 mode_id_blob;

	struct clv_event_source *repaint_event;
};

struct drm_backend {
	struct clv_backend base;
	struct clv_compositor *c;

	struct clv_event_loop *loop;

	struct clv_region canvas;

	char dev_node[128];
	s32 drm_fd;

	u32 cursor_w, cursor_h;

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

static void drm_fb_destroy(struct drm_fb *fb)
{
	if (fb->fb_id != 0)
		drmModeRmFB(fb->fd, fb->fb_id);
	free(fb);
}

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

static struct drm_fb *drm_fb_ref(struct drm_fb *fb)
{
	fb->refcnt++;
	return fb;
}

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
	fb->fd = b->drm_fd;

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
			drm_err("failed to create drm fb: %s", strerror(errno));
		goto err_free;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_gbm);

	return fb;

err_free:
	free(fb);
	return NULL;
}

static void drm_fb_destroy_dmabuf(struct drm_fb *fb)
{
	if (fb->bo)
		gbm_bo_destroy(fb->bo);
	drm_fb_destroy(fb);
}

static void drm_fb_unref(struct drm_fb *fb)
{
	if (!fb)
		return;

	assert(fb->refcnt > 0);
	if (--fb->refcnt > 0)
		return;

	switch (fb->type) {
	case DRM_BUF_CURSOR:
		gbm_bo_destroy(fb->bo);
		break;
	case DRM_BUF_GBM_SURFACE:
		gbm_surface_release_buffer(fb->gbm_surface, fb->bo);
		break;
	case DRM_BUF_DMABUF:
		drm_fb_destroy_dmabuf(fb);
		break;
	default:
		assert(NULL);
		break;
	}
}

static s32 set_drm_caps(struct drm_backend *b)
{
	u64 cap;
	s32 ret;
	struct clv_compositor *c = b->c;
	
	ret = drmGetCap(b->drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1) {
		drm_info("DRM TIMESTAMP: MONOTONIC");
		c->clock_type = CLOCK_MONOTONIC;
	} else {
		drm_info("DRM TIMESTAMP: REALTIME");
		c->clock_type = CLOCK_REALTIME;
	}

	if (drmSetClientCap(b->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		drm_err("DRM_CLIENT_CAP_UNIVERSAL_PLANES not supported");
		return -1;
	} else {
		drm_info("DRM_CLIENT_CAP_UNIVERSAL_PLANES supported");
	}

	if (drmSetClientCap(b->drm_fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		drm_err("DRM_CLIENT_CAP_ATOMIC not supported");
		return -1;
	} else {
		drm_info("DRM_CLIENT_CAP_ATOMIC supported");
	}

#ifdef PLATFORM_RK3399
	b->cursor_w = 128;
	b->cursor_h = 128;
#else
	ret = drmGetCap(b->drm_fd, DRM_CAP_CURSOR_WIDTH, &cap);
	if (ret) {
		drm_warn("DRM_CAP_CURSOR_WIDTH not supported");
		b->cursor_w = 64;
	} else {
		drm_info("DRM_CAP_CURSOR_WIDTH: %lu", cap);
		b->cursor_w = cap;
	}

	ret = drmGetCap(b->drm_fd, DRM_CAP_CURSOR_HEIGHT, &cap);
	if (ret) {
		drm_warn("DRM_CAP_CURSOR_HEIGHT not supported");
		b->cursor_h = 64;
	} else {
		drm_info("DRM_CAP_CURSOR_HEIGHT: %lu", cap);
		b->cursor_h = cap;
	}
#endif

	return 0;
}

static void drm_output_update_msc(struct drm_output *output, u32 seq)
{
	u64 msc_hi = output->base.msc >> 32;

	if (seq < (output->base.msc & 0xffffffff))
		msc_hi++;

	output->base.msc = (msc_hi << 32) + seq;
}

static struct drm_pending_state *drm_pending_state_alloc(struct drm_backend *b);
static struct drm_output_state *drm_output_state_dup(
					struct drm_output_state *src,
					struct drm_pending_state *pending_state,
					s32 reset);

static struct drm_output_state *drm_output_get_disable_state(
					struct drm_pending_state *pending_state,
					struct drm_output *output)
{
	struct drm_output_state *output_state;

	output_state = drm_output_state_dup(output->state_cur,pending_state, 1);
	output_state->dpms = CLV_DPMS_OFF;

	return output_state;
}

static void drm_output_state_free(struct drm_output_state *state);
static s32 drm_pending_state_apply_atomic(
				struct drm_pending_state *pending_state,
				s32 is_async);

/**
 * Mark a drm_output_state (the output's last state) as complete. This handles
 * any post-completion actions such as updating the repaint timer, disabling the
 * output, and finally freeing the state.
 */
static void drm_output_update_complete(struct drm_output *output,
				       u32 sec, u32 usec)
{
	struct drm_backend *b = container_of(output->base.c->backend,
					     struct drm_backend, base);
	struct drm_plane_state *ps;
	struct timespec ts;

	list_for_each_entry(ps, &output->state_cur->plane_states, link)
		ps->complete = 1;

	drm_output_state_free(output->state_last);
	output->state_last = NULL;

	if (output->disable_pending) {
		output->disable_pending = 0;
		output->dpms_off_pending = 0;
		clv_output_disable(&output->base);
		return;
	} else if (output->dpms_off_pending) {
		struct drm_pending_state *pending = drm_pending_state_alloc(b);
		output->dpms_off_pending = 0;
		drm_output_get_disable_state(pending, output);
		drm_pending_state_apply_atomic(pending, 0);
		return;
	} else if (output->state_cur->dpms == CLV_DPMS_OFF
		    && output->base.repaint_status
		        != REPAINT_AWAITING_COMPLETION) {
		/* DPMS can happen to us either in the middle of a repaint
		 * cycle (when we have painted fresh content, only to throw it
		 * away for DPMS off), or at any other random point. If the
		 * latter is true, then we cannot go through finish_frame,
		 * because the repaint machinery does not expect this. */
		return;
	}

	ts.tv_sec = sec;
	ts.tv_nsec = usec * 1000;
	clv_output_finish_frame(&output->base, &ts, 0);
}

static void page_flip_handler(s32 fd, u32 frame, u32 sec, u32 usec,
			      u32 crtc_id, void *data)
{
	struct drm_backend *b = data;
	s32 f = 0;
	struct drm_output *output;

	list_for_each_entry(output, &b->outputs, link) {
		if (output->crtc_id == crtc_id) {
			f = 1;
			break;
		}
	}
	assert(f);

	/* During the initial modeset, we can disable CRTCs which we don't
	 * actually handle during normal operation; this will give us events
	 * for unknown outputs. Ignore them. */
	if (!output || !output->base.enabled)
		return;

	drm_output_update_msc(output, frame);

	drm_debug("[atomic][CRTC: %u] page flip processing started", crtc_id);
	assert(output->atomic_complete_pending);
	output->atomic_complete_pending = 0;

	drm_output_update_complete(output, sec, usec);
	drm_debug("[atomic] [CRTC: %u] page flip processing end", crtc_id);
}

static void vblank_handler(s32 fd, u32 frame, u32 sec, u32 usec, void *data)
{
	assert(0);
}

static s32 drm_event_proc(s32 fd, u32 mask, void *data)
{
	struct drm_backend *b = data;
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

static s32 drm_output_update_modelist_from_heads(struct drm_output *output);

static void update_connectors(struct drm_backend *b)
{
	struct drm_output *output;
	struct drm_head *head;

	list_for_each_entry(output, &b->outputs, link) {
		if (!output->head)
			continue;
		head = output->head;
		if (head->connector) {
			drmModeFreeConnector(head->connector);
			head->connector = NULL;
		}
		head->connector = drmModeGetConnector(b->drm_fd,
					b->res->connectors[head->index]);
		if (!head->connector)
			continue;
		drm_output_update_modelist_from_heads(output);
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

static void *drm_repaint_begin(struct clv_compositor *c)
{
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_pending_state *ps;

	ps = drm_pending_state_alloc(b);
	b->repaint_data = ps;
	drm_debug("[repaint] Begin repaint, pending state %p", ps);
	return ps;
}

static void drm_pending_state_free(struct drm_pending_state *pending_state);

static void drm_repaint_cancel(struct clv_compositor *c, void *repaint_data)
{
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_pending_state *ps = repaint_data;

	drm_pending_state_free(ps);
	drm_debug("[repaint] Cancel repaint, pending state %p", ps);
	b->repaint_data = NULL;
}

static void drm_plane_state_free(struct drm_plane_state *state, s32 force);

static void drm_output_assign_state(struct drm_output_state *state,
				    s32 is_async)
{
	struct drm_output *output = state->output;
	struct drm_backend *b = container_of(output->base.c->backend,
					     struct drm_backend, base);
	struct drm_plane_state *plane_state;
	struct drm_plane *plane;

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

	/* Replace state_cur on each affected plane with the new state, being
	 * careful to dispose of orphaned (but only orphaned) previous state.
	 * If the previous state is not orphaned (still has an output_state
	 * attached), it will be disposed of by freeing the output_state. */
	list_for_each_entry(plane_state, &state->plane_states, link) {
		plane = plane_state->plane;
		if (plane->state_cur && !plane->state_cur->output_state)
			drm_plane_state_free(plane->state_cur, 1);
		plane->state_cur = plane_state;

		if (!is_async) {
			plane_state->complete = 1;
			continue;
		}
	}
}

static s32 drm_mode_ensure_blob(struct drm_backend *b, struct drm_mode *mode)
{
	s32 ret;

	if (mode->blob_id)
		return 0;

	ret = drmModeCreatePropertyBlob(b->drm_fd,
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

static s32 drm_output_apply_state_atomic(struct drm_output_state *state,
					 drmModeAtomicReq *req,
					 u32 *flags)
{
	struct drm_output *output = state->output;
	struct drm_backend *b = container_of(output->base.c->backend,
					     struct drm_backend, base);
	struct drm_plane_state *plane_state;
	struct drm_mode *current_mode = container_of(output->base.current_mode,
						     struct drm_mode, base);
	struct drm_head *head;
	s32 ret = 0;

	drm_debug("\t\t[atomic] applying output (%u) state", output->index);

	if (state->dpms != output->state_cur->dpms) {
		drm_debug("\t\t\t[atomic] DPMS state differs, modeset OK");
		*flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	if (state->dpms == CLV_DPMS_ON) {
		drm_debug("ret = %d %s", ret, strerror(errno));
		/* No need for the DPMS property, since it is implicit in
		 * routing and CRTC activity. */
		ret |= drmModeAtomicAddProperty(req, output->head->connector_id,
						output->head->prop_crtc_id,
						output->crtc_id);
		drm_debug("\t\t\t[CONN:%u] %u (%s) -> %u (0x%x)",
			  output->head->connector_id,
			  output->head->prop_crtc_id, "CONNECTOR's CRTC_ID",
			  output->crtc_id, output->crtc_id);
		drm_debug("ret = %d %s", ret, strerror(errno));

		ret = drm_mode_ensure_blob(b, current_mode);
		if (ret != 0)
			return ret;

		ret |= drmModeAtomicAddProperty(req, output->crtc_id,
						output->prop_mode_id,
						current_mode->blob_id);
		drm_debug("\t\t\t[CRTC:%u] %u (%s) -> %u (0x%x)",
			  output->crtc_id,
			  output->prop_mode_id, "CRTC's MODE ID",
			  current_mode->blob_id, current_mode->blob_id);
		drm_debug("ret = %d %s", ret, strerror(errno));

		ret |= drmModeAtomicAddProperty(req, output->crtc_id,
						output->prop_active,
						1);
		drm_debug("\t\t\t[CRTC:%u] %u (%s) -> %u (0x%x)",
			  output->crtc_id,
			  output->prop_active, "CRTC's ACTIVE",
			  1, 1);
		drm_debug("ret = %d %s", ret, strerror(errno));
	} else {
		ret |= drmModeAtomicAddProperty(req, output->crtc_id,
						output->prop_mode_id,
						0);
		drm_debug("\t\t\t[CRTC:%u] %u (%s) -> %u (0x%x)",
			  output->crtc_id,
			  output->prop_mode_id, "CRTC's MODE ID",
			  0, 0);

		ret |= drmModeAtomicAddProperty(req, output->crtc_id,
						output->prop_active,
						0);
		drm_debug("\t\t\t[CRTC:%u] %u (%s) -> %u (0x%x)",
			  output->crtc_id,
			  output->prop_active, "CRTC's MODE_ID",
			  0, 0);

		/* No need for the DPMS property, since it is implicit in
		 * routing and CRTC activity. */
		ret |= drmModeAtomicAddProperty(req, output->head->connector_id,
						output->head->prop_crtc_id,
						0);
		drm_debug("\t\t\t[CONN:%u] %u (%s) -> %u (0x%x)",
			  output->head->connector_id,
			  output->head->prop_crtc_id, "CONNECTOR's CRTC_ID",
			  0, 0);
	}

//	if (ret != 0) {
//		drm_err("couldn't set atomic CRTC/connector state %s",
//			strerror(errno));
//		return ret;
//	}

	list_for_each_entry(plane_state, &state->plane_states, link) {
		drm_debug("=======plane_state = %p state = %p", plane_state,
				state);
		struct drm_plane *plane = plane_state->plane;
		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_fb_id,
						plane_state->fb
						    ? plane_state->fb->fb_id:0);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_fb_id, "PLANE's FB_ID",
			  plane_state->fb ? plane_state->fb->fb_id : 0);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_crtc_id,
						plane_state->fb
						    ? output->crtc_id : 0);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_crtc_id,
			  "PLANE's CRTC_ID",
			  plane_state->fb ? output->crtc_id : 0);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_src_x,
						plane_state->src_x);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_src_x,
			  "PLANE's SRC_X", plane_state->src_x);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_src_y,
						plane_state->src_y);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_src_y,
			  "PLANE's SRC_Y", plane_state->src_y);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_src_w,
						plane_state->src_w);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_src_w,
			  "PLANE's SRC_W", plane_state->src_w);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_src_h,
						plane_state->src_h);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_src_h,
			  "PLANE's SRC_H", plane_state->src_h);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_crtc_x,
						plane_state->crtc_x);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_crtc_x,
			  "PLANE's CRTC_X", plane_state->crtc_x);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_crtc_y,
						plane_state->crtc_y);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_crtc_y,
			  "PLANE's CRTC_Y", plane_state->crtc_y);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_crtc_w,
						plane_state->crtc_w);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_crtc_w,
			  "PLANE's CRTC_W", plane_state->crtc_w);

		ret |= drmModeAtomicAddProperty(req, plane->plane_id,
						plane->prop_crtc_h,
						plane_state->crtc_h);
		drm_debug("\t\t\t[Plane:%u] %u (%s) -> %u",
			  plane->plane_id, plane->prop_crtc_h,
			  "PLANE's CRTC_H", plane_state->crtc_h);

//		if (ret != 0) {
//			drm_err("couldn't set plane state");
//			return ret;
//		}
	}

	return 0;
}

static s32 drm_pending_state_apply_atomic(
				struct drm_pending_state *pending_state,
				s32 is_async)
{
	struct drm_backend *b = pending_state->b;
	struct drm_output_state *output_state, *tmp;
	struct drm_plane *plane;
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	u32 flags;
	s32 ret = 0;

	if (!req)
		return -1;

	if (is_async) {
		flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	} else {
		flags = 0;
	}

	if (b->state_invalid) {
		struct clv_head *head_base;
		struct drm_head *head;
		struct drm_output *crtc;
		s32 err;

		drm_debug("\t\t[atomic] previous state invalid; "
			     "starting with fresh state");

		/* If we need to reset all our state (e.g. because we've
		 * just started, or just been VT-switched in), explicitly
		 * disable all the CRTCs and connectors we aren't using. */
		list_for_each_entry(head_base,
				 &b->c->heads, link) {
			if (head_base->output && head_base->output->enabled)
				continue;

			head = container_of(head_base, struct drm_head, base);

			drm_debug("\t\t[atomic] disabling inactive head %u",
				  head->index);

			err = drmModeAtomicAddProperty(req, head->connector_id,
						       head->prop_crtc_id, 0);
			drm_debug("\t\t\t[CONN:%u] %u (%s) -> 0",
				  head->connector_id,
				  head->prop_crtc_id,
				  "DRM CONNECTOR's CRTC_ID");
			if (err <= 0) {
				drm_err("failed to disable connector");
				ret = -1;
			}
		}

		list_for_each_entry(crtc, &b->outputs, link) {
			if (crtc->head)
				continue;
			drm_debug("\t\t[atomic] disabling unused CRTC %u",
				  crtc->crtc_id);

			drm_debug("\t\t\t[CRTC:%u] %u (%s) -> 0",
				  crtc->crtc_id,
				  crtc->prop_active, "CRTC's ACTIVE");
			err = drmModeAtomicAddProperty(req, crtc->crtc_id,
						       crtc->prop_active, 0);
			if (err <= 0)
				ret = -1;

			drm_debug("\t\t\t[CRTC:%u] %u (%s) -> 0",
				  crtc->crtc_id,
				  crtc->prop_mode_id, "CRTC's MODE_ID");
			err = drmModeAtomicAddProperty(req, crtc->crtc_id,
						       crtc->prop_mode_id, 0);
			if (err <= 0)
				ret = -1;
		}

		/* Disable all the planes; planes which are being used will
		 * override this state in the output-state application. */
		list_for_each_entry(plane, &b->planes, link_b) {
			drm_debug("\t\t[atomic] starting with plane %u "
				  "disabled",
				  plane->plane_id);
			drmModeAtomicAddProperty(req, plane->plane_id,
						 plane->prop_crtc_id, 0);
			drmModeAtomicAddProperty(req, plane->plane_id,
						 plane->prop_fb_id, 0);
		}

		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	list_for_each_entry(output_state, &pending_state->output_states, link) {
		if (!is_async)
			assert(output_state->dpms == CLV_DPMS_OFF);
		ret |= drm_output_apply_state_atomic(output_state, req, &flags);
	}

//	if (ret != 0) {
//		drm_err("[atomic] couldn't compile atomic state");
//		goto out;
//	}

	ret = drmModeAtomicCommit(b->drm_fd, req, flags, b);
	drm_debug("[atomic] drmModeAtomicCommit");

	if (ret != 0) {
		drm_err("[atomic] couldn't commit new state: %s",
			strerror(errno));
		goto out;
	}

	list_for_each_entry_safe(output_state, tmp,
				 &pending_state->output_states, link)
		drm_output_assign_state(output_state, is_async);

	b->state_invalid = 0;

	assert(list_empty(&pending_state->output_states));

out:
	drmModeAtomicFree(req);
	drm_pending_state_free(pending_state);
	return ret;
}

static s32 drm_pending_state_apply(struct drm_pending_state *pending_state)
{
	return drm_pending_state_apply_atomic(pending_state, 1);
}

static void drm_repaint_flush(struct clv_compositor *c, void *repaint_data)
{
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_pending_state *ps = repaint_data;

	drm_pending_state_apply(ps);
	drm_debug("[repaint] Flush, pending state %p", ps);
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

/*
 * Allocate a new, empty, plane state.
 */
static struct drm_plane_state *drm_plane_state_alloc(
					struct drm_plane *plane,
					struct drm_output_state *output_state)
{
	struct drm_plane_state *state = calloc(1, sizeof(*state));

	if (!state)
		return NULL;
	drm_debug("==================================== %p", output_state);

	state->output_state = output_state;
	state->plane = plane;

	/* Here we only add the plane state to the desired link, and not
	 * set the member. Having an output pointer set means that the
	 * plane will be displayed on the output; this won't be the case
	 * when we go to disable a plane. In this case, it must be part of
	 * the commit (and thus the output state), but the member must be
	 * NULL, as it will not be on any output when the state takes
	 * effect.
	 */
	if (output_state)
		list_add_tail(&state->link, &output_state->plane_states);

	return state;
}

/*
 * Free an existing plane state. As a special case, the state will not
 * normally be freed if it is the current state; see drm_plane_set_state.
 */
static void drm_plane_state_free(struct drm_plane_state *state, s32 force)
{
	if (!state)
		return;

	list_del(&state->link);
	state->output_state = NULL;

	if (force || state != state->plane->state_cur) {
		drm_fb_unref(state->fb);
		free(state);
	}
}

/*
 * Duplicate an existing plane state into a new plane state, storing it within
 * the given output state. If the output state already contains a plane state
 * for the drm_plane referenced by 'src', that plane state is freed first.
 */
static struct drm_plane_state *drm_plane_state_dup(
					struct drm_plane_state *src,
					struct drm_output_state *output_state)
{
	struct drm_plane_state *dst = calloc(1, sizeof(*dst));
	struct drm_plane_state *old, *tmp;

	if (!src)
		return NULL;

	if (!dst)
		return NULL;

	*dst = *src;

	list_for_each_entry_safe(old, tmp, &output_state->plane_states, link) {
		/* Duplicating a plane state into the same output state, so
		 * it can replace itself with an identical copy of itself,
		 * makes no sense. */
		assert(old != src);
		if (old->plane == dst->plane)
			drm_plane_state_free(old, 0);
	}

	list_add_tail(&dst->link, &output_state->plane_states);
	if (src->fb)
		dst->fb = drm_fb_ref(src->fb);
	dst->output_state = output_state;
	dst->complete = 0;

	return dst;
}

static struct drm_plane_state *drm_output_state_get_existing_plane(
					struct drm_output_state *state_output,
					struct drm_plane *plane)
{
	struct drm_plane_state *ps;

	list_for_each_entry(ps, &state_output->plane_states, link) {
		if (ps->plane == plane)
			return ps;
	}

	return NULL;
}

static struct drm_plane_state *drm_output_state_get_plane(
					struct drm_output_state *state_output,
					struct drm_plane *plane)
{
	struct drm_plane_state *ps;

	ps = drm_output_state_get_existing_plane(state_output, plane);
	if (ps)
		return ps;

	return drm_plane_state_alloc(plane, state_output);
}

/*
 * Allocate a new, empty drm_output_state.
 * This should not generally be used in the repaint cycle.
 */
static struct drm_output_state *drm_output_state_alloc(
			struct drm_output *output,
			struct drm_pending_state *pending_state)
{
	struct drm_output_state *state = calloc(1, sizeof(*state));

	if (!state)
		return NULL;

	state->output = output;
	state->dpms = CLV_DPMS_OFF;
	state->pending_state = pending_state;
	if (pending_state)
		list_add_tail(&state->link, &pending_state->output_states);

	INIT_LIST_HEAD(&state->plane_states);

	return state;
}

/*
 * Duplicate an existing drm_output_state into a new one.
 * This is generally used during the repaint cycle, to capture the existing
 * state of an output and modify it to create a new state to be used.
 *
 * The 'reset' determines whether the output will be reset to an a blank state,
 * or an exact mirror of the current state.
 *     0 - not reset to emtpy state
 *     1 - reset to empty state
 */
static struct drm_output_state *drm_output_state_dup(
					struct drm_output_state *src,
					struct drm_pending_state *pending_state,
					s32 reset)
{
	struct drm_output_state *dst = calloc(1, sizeof(*dst));
	struct drm_plane_state *ps;

	if (!dst)
		return NULL;

	*dst = *src;

	dst->pending_state = pending_state;
	if (pending_state)
		list_add_tail(&dst->link, &pending_state->output_states);

	INIT_LIST_HEAD(&dst->plane_states);

	list_for_each_entry(ps, &src->plane_states, link) {
		/* TODO check */
		/* Don't carry planes which are now disabled; these should be
		 * free for other outputs to reuse. */
		if (!ps->output)
			continue;

		if (reset)
			drm_plane_state_alloc(ps->plane, dst);
		else
			drm_plane_state_dup(ps, dst);
	}

	return dst;
}

/*
 * Free an unused drm_output_state.
 */
static void drm_output_state_free(struct drm_output_state *state)
{
	struct drm_plane_state *ps, *next;

	if (!state)
		return;

	list_for_each_entry_safe(ps, next, &state->plane_states, link) {
		drm_plane_state_free(ps, 0);
	}

	list_del(&state->link);

	free(state);
}

static struct drm_pending_state *drm_pending_state_alloc(struct drm_backend *b)
{
	struct drm_pending_state *ps;

	ps = calloc(1, sizeof(*ps));
	if (!ps)
		return NULL;

	ps->b = b;
	INIT_LIST_HEAD(&ps->output_states);

	return ps;
}

static void drm_pending_state_free(struct drm_pending_state *pending_state)
{
	struct drm_output_state *output_state, *tmp;

	if (!pending_state)
		return;

	list_for_each_entry_safe(output_state, tmp,
				 &pending_state->output_states,
				 link) {
		drm_output_state_free(output_state);
	}

	free(pending_state);
}

static struct drm_output_state *drm_pending_state_get_output(
					struct drm_pending_state *pending_state,
					struct drm_output *output)
{
	struct drm_output_state *output_state;

	list_for_each_entry(output_state, &pending_state->output_states, link) {
		if (output_state->output == output)
			return output_state;
	}

	return NULL;
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
	struct drm_mode *dm;

	list_for_each_entry(m, &output->base.modes, link) {
		dm = container_of(m, struct drm_mode, base);
		drm_info("%ux%u@%.1f%s%s, %.1f MHz",
			 m->w, m->h, m->refresh / 1000.0f,
			 m->flags & CLV_OUTPUT_MODE_PREFERRED ?
			 ", preferred" : "",
			 m->flags & CLV_OUTPUT_MODE_CURRENT ?
			 ", current" : "",
			 dm->mode_info.clock / 1000.0);
	}
}

static s32 drm_output_switch_mode(struct clv_output *output_base,
				  struct clv_mode *mode)
{
	return 0;
}

static u32 drm_waitvblank_pipe(struct drm_output *output)
{
	if (output->index > 1)
		return (output->index << DRM_VBLANK_HIGH_CRTC_SHIFT) &
				DRM_VBLANK_HIGH_CRTC_MASK;
	else if (output->index > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static void drm_output_start_repaint_loop(struct clv_output *base)
{
	struct drm_output *output = container_of(base, struct drm_output, base);
	struct drm_pending_state *ps;
	struct drm_plane *primary_plane = output->primary_plane;
	struct drm_backend *b = container_of(base->c->backend,
					     struct drm_backend, base);
	struct timespec ts, now;
	struct timespec vbl2now;
	s64 refresh_nsec;
	s32 ret;
	drmVBlank vbl = {
		.request.type = DRM_VBLANK_RELATIVE,
		.request.sequence = 0,
		.request.signal = 0,
	};

	drm_debug("TAG");
	if (output->disable_pending)
		return;

	if (!output->primary_plane->state_cur) {
		goto finish_frame;
	}

	if (!output->primary_plane->state_cur->fb) {
		goto finish_frame;
	}

	if (b->state_invalid)
		goto finish_frame;

	assert(primary_plane->state_cur->output == output);
	vbl.request.type |= drm_waitvblank_pipe(output);
	ret = drmWaitVBlank(b->drm_fd, &vbl);
	if ((ret == 0) && (vbl.reply.tval_sec > 0 || vbl.reply.tval_usec > 0)) {
		ts.tv_sec = vbl.reply.tval_sec;
		ts.tv_nsec = vbl.reply.tval_usec * 1000;

		/* Valid timestamp for most recent vblank - not stale?
		 * Stale ts could happen on Linux 3.17+, so make sure it
		 * is not older than 1 refresh duration since now.
		 */
		clock_gettime(b->c->clock_type, &now);
		timespec_sub(&vbl2now, &now, &ts);
		refresh_nsec =
			millihz_to_nsec(output->base.current_mode->refresh);
		if (timespec_to_nsec(&vbl2now) < refresh_nsec) {
			drm_output_update_msc(output, vbl.reply.sequence);
			clv_output_finish_frame(base, &ts,
						PRESENTATION_FEEDBACK_INVALID);
			return;
		}
	}

	/* Immediate query didn't provide valid timestamp.
	 * Use pageflip fallback.
	 */
	assert(!output->state_last);

	ps = drm_pending_state_alloc(b);
	drm_output_state_dup(output->state_cur, ps, 0);

	ret = drm_pending_state_apply(ps);
	if (ret != 0) {
		drm_err("failed to apply repaint-start state failed: %s",
			strerror(errno));
		goto finish_frame;
	}

	return;

finish_frame:
	/* if we cannot page-flip, immediately finish frame */
	clv_output_finish_frame(base, NULL, PRESENTATION_FEEDBACK_INVALID);
}

/**
 * Remove a plane state from an output state; if the plane was previously
 * enabled, then replace it with a disabling state. This ensures that the
 * output state was untouched from it was before the plane state was
 * modified by the caller of this function.
 *
 * This is required as drm_output_state_get_plane may either allocate a
 * new plane state, in which case this function will just perform a matching
 * drm_plane_state_free, or it may instead repurpose an existing disabling
 * state (if the plane was previously active), in which case this function
 * will reset it.
 */
static void drm_plane_state_put_back(struct drm_plane_state *state)
{
	struct drm_output_state *state_output;
	struct drm_plane *plane;

	if (!state)
		return;

	state_output = state->output_state;
	plane = state->plane;
	drm_plane_state_free(state, 0);

	/* Plane was previously disabled; no need to keep this temporary
	 * state around. */
	if (!plane->state_cur->fb)
		return;

	(void)drm_plane_state_alloc(plane, state_output);
}

static struct drm_fb *drm_output_render_gl(struct drm_output_state *state)
{
	struct drm_output *output = state->output;
	struct drm_backend *b = container_of(output->base.c->backend,
					     struct drm_backend,
					     base);
	struct gbm_bo *bo;
	struct drm_fb *ret;

	output->base.c->renderer->repaint_output(&output->base);

	bo = gbm_surface_lock_front_buffer(output->gbm_surface);
	if (!bo) {
		drm_err("failed to lock front buffer: %s", strerror(errno));
		return NULL;
	}

	/* The renderer always produces an opaque image. */
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
	struct clv_compositor *c = output->base.c;
	struct drm_plane_state *primary_state;
	struct drm_plane *primary_plane = output->primary_plane;
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_fb *fb;

	/* If we already have a client buffer promoted to scanout, then we don't
	 * want to render. */
	primary_state = drm_output_state_get_plane(state,
						   output->primary_plane);
	if (primary_state->fb)
		return;

//	if (primary_plane->state_cur->fb &&
//	    (primary_plane->state_cur->fb->type == DRM_BUF_GBM_SURFACE)) {
//		fb = drm_fb_ref(primary_plane->state_cur->fb);
//	} else {
		fb = drm_output_render_gl(state);
//	}

	if (!fb) {
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
	struct drm_pending_state *ps = repaint_data;
	struct drm_output *output = container_of(base, struct drm_output, base);
	struct drm_output_state *state = NULL;
	struct drm_plane_state *primary_state;

	drm_debug("TAG ps = %p", ps);
	if (output->disable_pending)
		goto error;

	assert(!output->state_last);

	/* If planes have been disabled in the core, we might not have
	 * hit assign_planes at all, so might not have valid output state
	 * here. */
	state = drm_pending_state_get_output(ps, output);
	if (!state) {
		drm_debug("TAG ps = %p", ps);
		state = drm_output_state_dup(output->state_cur,
					     ps, 1);
	}
	state->dpms = CLV_DPMS_ON;

	drm_output_render(state);
	drm_debug("TAG ps = %p", ps);
	primary_state = drm_output_state_get_plane(state,
						   output->primary_plane);
	drm_debug("TAG ps = %p", ps);
	if (!primary_state || !primary_state->fb)
		goto error;

	return 0;

error:
	drm_output_state_free(state);
	return -1;
}

static void drm_assign_planes(struct clv_output *output_base,
			      void *repaint_data)
{

}

static void drm_set_dpms(struct clv_output *output_base, enum clv_dpms level)
{

}

static void drm_output_deinit_cursor_egl(struct drm_output *output)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(output->gbm_cursor_fb); i++) {
		drm_fb_unref(output->gbm_cursor_fb[i]);
		output->gbm_cursor_fb[i] = NULL;
	}
}

static s32 drm_output_init_cursor_egl(struct drm_output *output,
				      struct drm_backend *b)
{
	u32 i;
	struct gbm_bo *bo;

	if (!output->cursor_plane)
		return 0;

	for (i = 0; i < ARRAY_SIZE(output->gbm_cursor_fb); i++) {
		bo = gbm_bo_create(b->gbm, b->cursor_w, b->cursor_h,
				   GBM_FORMAT_ARGB8888,
				   GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
		if (!bo)
			goto error;

		output->gbm_cursor_fb[i] =
			drm_fb_get_from_bo(bo, b, 0, DRM_BUF_CURSOR);
		if (!output->gbm_cursor_fb[i]) {
			gbm_bo_destroy(bo);
			goto error;
		}
	}

	return 0;

error:
	drm_debug("failed to create gbm cursor bo");
	drm_output_deinit_cursor_egl(output);
	return -1;
}

static void drm_output_deinit_egl(struct drm_output *output)
{
	struct clv_compositor *c = output->base.c;
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);

	if (output->primary_plane->state_cur->fb) {
		drm_plane_state_free(output->primary_plane->state_cur, 1);
		output->primary_plane->state_cur = drm_plane_state_alloc(
			output->primary_plane, NULL);
		output->primary_plane->state_cur->complete = 1;
	}

	gl_renderer->output_destroy(&output->base);
	gbm_surface_destroy(output->gbm_surface);
	output->gbm_surface = NULL;
	drm_output_deinit_cursor_egl(output);
}

static s32 drm_output_init_egl(struct drm_output *output, struct drm_backend *b)
{
	struct clv_mode *mode = output->base.current_mode;
	s32 vid;
	
	output->gbm_surface = gbm_surface_create(b->gbm, mode->w, mode->h,
						 b->gbm_format,
						 output->gbm_bo_flags);

	if (!output->gbm_surface) {
		drm_err("failed to create gbm surface");
		return -1;
	}

	if (gl_renderer->output_create(&output->base,
				       NULL,
				       (void *)(output->gbm_surface),
				       (s32 *)(&b->gbm_format), 1, &vid) < 0) {
		drm_err("failed to create gl renderer output state");
		gbm_surface_destroy(output->gbm_surface);
		output->gbm_surface = NULL;
		return -1;
	}

	drm_output_init_cursor_egl(output, b);

	return 0;
}

static void drm_output_destroy_mode(struct drm_backend *b,
				    struct drm_mode *mode)
{
	if (mode->blob_id)
		drmModeDestroyPropertyBlob(b->drm_fd, mode->blob_id);

	list_del(&mode->base.link);
	free(mode);
}

static void drm_mode_list_destroy(struct drm_backend *b,
				  struct list_head *mode_list)
{
	struct drm_mode *mode, *next;

	list_for_each_entry_safe(mode, next, mode_list, base.link) {
		drm_output_destroy_mode(b, mode);
	}
}

static struct clv_mode *drm_mode_create(const drmModeModeInfo *info)
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
		mode->base.flags |= CLV_OUTPUT_MODE_PREFERRED;

	INIT_LIST_HEAD(&mode->base.link);

	return &mode->base;
}

static void drm_output_try_add_mode(struct drm_output *output,
				    const drmModeModeInfo *info)
{
	struct clv_mode *m;

	if (info->flags & DRM_MODE_FLAG_INTERLACE)
		return;

	m = drm_mode_create(info);
	if (m)
		list_add_tail(&m->link, &output->base.modes);
}

static s32 drm_output_update_modelist_from_heads(struct drm_output *output)
{
	struct clv_compositor *c = output->base.c;
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_head *head;
	s32 i;

	head = output->head;
	if (!head->base.connected)
		return 0;

	drm_mode_list_destroy(b, &output->base.modes);

	assert(head);
	for (i = 0; i < head->connector->count_modes; i++)
		drm_output_try_add_mode(output, &head->connector->modes[i]);

	return 0;
}

static s32 drm_output_enable(struct clv_output *base)
{
	struct drm_backend *b = container_of(base->c->backend,
					     struct drm_backend,
					     base);
	struct drm_output *output = container_of(base, struct drm_output, base);

	drm_info("enabling crtc 0x%08X", output->crtc_id);
	if (drm_output_init_egl(output, b) < 0)
		return -1;

	output->base.start_repaint_loop = drm_output_start_repaint_loop;
	output->base.repaint = drm_output_repaint;
	output->base.assign_planes = drm_assign_planes;
	output->base.set_dpms = drm_set_dpms;
	output->base.switch_mode = drm_output_switch_mode;
	output->base.enabled = 1;

	drm_output_print_modes(output);
	
	return 0;
}

static s32 drm_output_disable(struct clv_output *base)
{
	struct drm_output *output = container_of(base, struct drm_output, base);

	if (output->atomic_complete_pending) {
		output->disable_pending = 1;
		return -1;
	}

	drm_info("disabling crtc 0x%08X", output->crtc_id);
	if (output->base.enabled)
		drm_output_deinit_egl(output);

	output->disable_pending = 0;

	return 0;
}

static void drm_plane_destroy(struct drm_plane *plane)
{
	if (!plane)
		return;

	if (plane->props)
		drmModeFreeObjectProperties(plane->props);
	if (plane->pl)
		drmModeFreePlane(plane->pl);

	list_del(&plane->link_b);
	free(plane);
}

static struct drm_plane *drm_plane_create(struct clv_layer_config *config,
					  struct drm_output *output)
{
	struct drm_plane *plane = calloc(1, sizeof(*plane));
	struct drm_backend *b = output->b;

	plane->b = b;
	INIT_LIST_HEAD(&plane->link);
	list_add_tail(&plane->link_b, &b->planes);
	plane->index = config->index;
	plane->plane_id = b->pres->planes[plane->index];
	plane->output = output;
	plane->pl = drmModeGetPlane(b->drm_fd, plane->plane_id);
	if (!plane->pl) {
		free(plane);
		return NULL;
	}
	plane->state_cur = drm_plane_state_alloc(plane, NULL);;
	plane->state_cur->complete = 1;

	plane->props = drmModeObjectGetProperties(b->drm_fd, plane->plane_id,
						  DRM_MODE_OBJECT_PLANE);
	plane->type = config->type;
	plane->prop_crtc_id = get_prop_id(b->drm_fd, plane->props, "CRTC_ID");
	plane->prop_fb_id = get_prop_id(b->drm_fd, plane->props, "FB_ID");
	plane->prop_crtc_x = get_prop_id(b->drm_fd, plane->props, "CRTC_X");
	plane->prop_crtc_y = get_prop_id(b->drm_fd, plane->props, "CRTC_Y");
	plane->prop_crtc_w = get_prop_id(b->drm_fd, plane->props, "CRTC_W");
	plane->prop_crtc_h = get_prop_id(b->drm_fd, plane->props, "CRTC_H");
	plane->prop_src_x = get_prop_id(b->drm_fd, plane->props, "SRC_X");
	plane->prop_src_y = get_prop_id(b->drm_fd, plane->props, "SRC_Y");
	plane->prop_src_w = get_prop_id(b->drm_fd, plane->props, "SRC_W");
	plane->prop_src_h = get_prop_id(b->drm_fd, plane->props, "SRC_H");

	return plane;
}

static void drm_encoder_destroy(struct drm_encoder *encoder)
{
	if (encoder->enc)
		drmModeFreeEncoder(encoder->enc);

	free(encoder);
}

static struct drm_encoder *drm_encoder_create(struct clv_encoder_config *enc,
					      struct drm_output *output,
					      struct drm_head *head)
{
	struct drm_encoder *encoder = calloc(1, sizeof(*encoder));

	if (!encoder)
		return NULL;

	encoder->output = output;
	encoder->head = head;
	encoder->b = output->b;
	encoder->index = enc->index;
	encoder->encoder_id = encoder->b->res->encoders[encoder->index];
	encoder->enc = drmModeGetEncoder(encoder->b->drm_fd,
					 encoder->encoder_id);
	if (!encoder->enc) {
		free(encoder);
		return NULL;
	}

	return encoder;
}

static void drm_output_destroy(struct clv_output *base)
{
	struct drm_plane *overlay, *next;
	struct drm_output *output = container_of(base, struct drm_output, base);

	if (!output)
		return;

	list_for_each_entry_safe(overlay, next, &output->overlay_planes, link) {
		list_del(&overlay->link);
		clv_plane_deinit(&overlay->base);
		drm_plane_destroy(overlay);
	}

	if (output->cursor_plane) {
		drm_plane_destroy(output->cursor_plane);
		clv_plane_deinit(&output->cursor_plane->base);
	}

	if (output->primary_plane) {
		drm_plane_destroy(output->primary_plane);
		clv_plane_deinit(&output->primary_plane->base);
	}

	if (output->encoder)
		drm_encoder_destroy(output->encoder);

	list_del(&output->link);

	if (output->props)
		drmModeFreeObjectProperties(output->props);

	if (output->crtc)
		drmModeFreeCrtc(output->crtc);

	clv_output_deinit(&output->base);
}

static struct clv_output *drm_output_create(struct clv_compositor *c,
					    struct clv_rect *render_area,
					    struct clv_head_config *head_config)
{
	struct drm_head *h;
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_output *output = calloc(1, sizeof(*output));
	s32 f = 0;
	struct drm_plane *overlay, *next;
	struct clv_layer_config *layer_config;

	if (!output)
		return NULL;

	output->b = b;
	output->gbm_bo_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
	list_add_tail(&output->link, &b->outputs);

	list_for_each_entry(h, &b->heads, link) {
		if (h->index == head_config->index) {
			f = 1;
			break;
		}
	}

	assert(f);
	output->head = h;
	output->encoder = drm_encoder_create(&head_config->encoder,
					     output, h);
	if (!output->encoder)
		goto error;
	
	output->index = head_config->encoder.output.index;
	output->crtc_id = b->res->crtcs[output->index];
	output->crtc = drmModeGetCrtc(b->drm_fd, output->crtc_id);
	if (!output->crtc)
		goto error;

	output->atomic_complete_pending = 0;
	output->disable_pending = 0;
	output->dpms_off_pending = 0;
	clv_plane_init(&c->primary_plane, c, NULL);
	output->primary_plane = drm_plane_create(
				head_config->encoder.output.primary_layer,
				output);
	if (!output->primary_plane)
		goto error;
	clv_plane_init(&output->primary_plane->base, c, &c->primary_plane);
	output->cursor_plane = drm_plane_create(
				head_config->encoder.output.cursor_layer,
				output);
	if (!output->primary_plane)
		goto error;
	clv_plane_init(&output->cursor_plane->base, c, NULL);

	INIT_LIST_HEAD(&output->overlay_planes);
	list_for_each_entry(layer_config,
			    &head_config->encoder.output.overlay_layers,
			    link) {
		overlay = drm_plane_create(layer_config, output);
		list_add_tail(&overlay->link, &output->overlay_planes);
		clv_plane_init(&overlay->base, c, &c->primary_plane);
	}

	output->props = drmModeObjectGetProperties(b->drm_fd, output->crtc_id,
						   DRM_MODE_OBJECT_CRTC);
	output->prop_active = get_prop_id(b->drm_fd, output->props, "ACTIVE");
	output->prop_mode_id = get_prop_id(b->drm_fd, output->props, "MODE_ID");

	clv_output_init(&output->base, c, render_area, output->index, &h->base);
	drm_output_update_modelist_from_heads(output);

	output->base.enable = drm_output_enable;
	output->base.destroy = drm_output_destroy;
	output->base.disable = drm_output_disable;

	output->state_cur = drm_output_state_alloc(output, NULL);

	b->state_invalid = 1;
	clv_output_schedule_repaint(&output->base);

	return &output->base;

error:
	if (output) {
		list_for_each_entry_safe(overlay, next, &output->overlay_planes,
					 link) {
			list_del(&overlay->link);
			clv_plane_deinit(&overlay->base);
			drm_plane_destroy(overlay);
		}
		if (output->cursor_plane) {
			drm_plane_destroy(output->cursor_plane);
			clv_plane_deinit(&output->cursor_plane->base);
		}
		if (output->primary_plane) {
			drm_plane_destroy(output->primary_plane);
			clv_plane_deinit(&output->primary_plane->base);
		}
		if (output->encoder)
			drm_encoder_destroy(output->encoder);
		list_del(&output->link);
		if (output->props)
			drmModeFreeObjectProperties(output->props);
		if (output->crtc)
			drmModeFreeCrtc(output->crtc);
		clv_output_deinit(&output->base);
	}
	return NULL;
}

static void drm_head_enumerate_mode(struct clv_head *base, u32 *w, u32 *h,
				    void **last)
{
	struct drm_head *head = container_of(base, struct drm_head, base);
	drmModeModeInfo *info, *p;

	if (head->connector->connection != DRM_MODE_CONNECTED) {
		*w = *h = 0;
		return;
	}

	info = head->connector->modes;
	if (*last) {
		p = (*last) + 1;
	} else {
		p = info;
	}

	if (p->flags & DRM_MODE_FLAG_INTERLACE)
		p++;

	if (p >= (info + head->connector->count_modes)) {
		*w = *h = 0;
		return;
	}
	
	*w = p->hdisplay;
	*h = p->vdisplay;

	*last = p;
}

static struct clv_head *drm_head_create(struct clv_compositor *c, s32 index)
{
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct drm_head *head = calloc(1, sizeof(*head));

	if (!head)
		return NULL;

	head->b = b;
	list_add_tail(&head->link, &b->heads);
	head->output = NULL;
	head->encoder = NULL;
	head->connector_id = b->res->connectors[index];
	head->connector = drmModeGetConnector(b->drm_fd, head->connector_id);
	if (!head->connector) {
		list_del(&head->link);
		free(head);
		return NULL;
	}

	head->index = index;
	head->props = drmModeObjectGetProperties(b->drm_fd,
						 head->connector_id,
						 DRM_MODE_OBJECT_CONNECTOR);
	if (!head->props) {
		drmModeFreeConnector(head->connector);
		list_del(&head->link);
		free(head);
		return NULL;
	}
	head->prop_crtc_id = get_prop_id(b->drm_fd, head->props, "CRTC_ID");
	head->prop_dpms = get_prop_id(b->drm_fd, head->props, "DPMS");
	head->base.c = c;
	head->output = NULL;
	head->base.connected =
		(head->connector->connection == DRM_MODE_CONNECTED ? 1 : 0);
	head->base.index = index;
	list_add_tail(&head->base.link, &c->heads);
	if (head->base.connected) {
		for (s32 i = 0; i < head->connector->count_modes; i++) {
			if (head->connector->modes[i].flags
			    & DRM_MODE_FLAG_INTERLACE)
				continue;
			head->base.best_mode = drm_mode_create(
						&head->connector->modes[i]);
			break;
		}
	}
	head->base.head_enumerate_mode = drm_head_enumerate_mode;
	return &head->base;
}

static void drm_backend_destroy(struct clv_compositor *c)
{
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);

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

	if (b->drm_fd > 0) {
		close(b->drm_fd);
		b->drm_fd = 0;
	}

	clv_region_fini(&b->canvas);
	free(b);

	c->backend = NULL;
}

struct clv_backend *drm_backend_create(struct clv_compositor *c)
{
	struct drm_backend *b;
	char *dev_node;
	s32 vid, i;

	drm_debug("backend create");
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
	
	b->drm_fd = open(b->dev_node, O_RDWR | O_CLOEXEC, 0644);
	if (b->drm_fd < 0) {
		drm_err("failed to open %s. %s", b->dev_node,
			strerror(errno));
		goto error;
	}

	if (set_drm_caps(b) < 0) {
		drm_err("failed to set DRM capabilities");
		goto error;
	}

	b->res = drmModeGetResources(b->drm_fd);
	if (!b->res) {
		drm_err("failed to get drmModeResource");
		goto error;
	}

	b->pres = drmModeGetPlaneResources(b->drm_fd);
	if (!b->pres) {
		drm_err("failed to get drmModePlaneResource");
		goto error;
	}

	b->gbm_format = GBM_FORMAT_ARGB8888;
	b->gbm = gbm_create_device(b->drm_fd);
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

	b->udev_drm_source = clv_event_loop_add_fd(b->loop, udev_monitor_get_fd(
							b->udev_monitor),
						   CLV_EVT_READABLE,
						   udev_drm_event_proc, b);
	if (!b->udev_drm_source)
		goto error;

	b->drm_source = clv_event_loop_add_fd(b->loop, b->drm_fd,
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

	renderer_create(c, (s32 *)&b->gbm_format, 1, 1, b->gbm, &vid);
	assert(c->renderer);
	gl_renderer = c->renderer;

	for (i = 0; i < b->res->count_connectors; i++)
		drm_head_create(c, i);
	clv_compositor_schedule_heads_changed(c);

	return &b->base;

error:
	drm_backend_destroy(c);
	return NULL;
}

void drm_set_dbg(u8 flag)
{
	drm_dbg = flag & 0x0F;
	gbm_dbg = (flag & 0xF0) >> 4;
}

