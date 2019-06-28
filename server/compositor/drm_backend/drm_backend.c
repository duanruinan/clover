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
#include <compositor.h>

static u8 drm_dbg = 16;
static u8 gbm_dbg = 16;

struct clv_renderer *renderer = NULL;

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
	DRM_BUF_DMABUF, /* sourced from other hardware. e.g. decoder, camera */
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
	struct list_head b_link;
};

struct drm_plane {
	struct clv_plane base;
	struct drm_backend *b;
	enum drm_plane_type type;

	u32 index;
	u32 plane_id;

	struct drm_output *output;
	struct list_head b_link;

	drmModePlanePtr pl;

	drmModeObjectProperties *props;
	u32 prop_type;
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

	u32 possible_crtcs;

	/* The last state submitted to the kernel for this plane. */
	struct drm_plane_state *state_cur;
};

struct drm_head {
	struct clv_head base;
	struct drm_backend *b;

	struct drm_output *output;
	struct drm_encoder *encoder;
	struct list_head b_link;

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

	struct drm_head *head;
	struct drm_encoder *encoder;
	struct list_head b_link;

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
	struct drm_plane *overlay_plane;

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

	enum clv_desktop_mode mode;
	struct clv_region canvas;

	char dev_node[DEV_NODE_LEN];
	s32 drm_fd;

	u32 cursor_w, cursor_h;
	clockid_t clk_id;

	struct udev *udev;
	struct clv_event_source *drm_source;

	struct udev_monitor *udev_monitor;
	struct clv_event_source *udev_drm_source;
	s32 sysnum;

	struct gbm_device *gbm;
	u32 gbm_format;

	void *repaint_data;

	drmModeRes *res;
	drmModePlaneRes *pres;

	struct list_head outputs;
	struct list_head heads;
	struct list_head encoders;
	struct list_head planes;
};

/* Initializes a clv_output object with enough data so an output can be
 * configured.
 *
 * Sets initial values for fields that are expected to be
 * configured either by compositors or backends.
 *
 * The index is used in logs, and can be used by compositors as a configuration
 * identifier.
 */
void clv_output_init(struct clv_output *output, struct clv_compositor *c,
		     u32 index)
{
	output->c = c;
	output->index = index;
	output->enabled = 0;

	output->head = NULL;

/* TODO check
	pixman_region32_init(&output->previous_damage);
*/
	INIT_LIST_HEAD(&output->modes);
	INIT_LIST_HEAD(&output->link);
}

void clv_output_init_area(struct clv_output *output, struct clv_box *box)
{
	output->render_area.pos.x = box->p1.x;
	output->render_area.pos.y = box->p1.y;
	output->render_area.w = box->p2.x - box->p1.x;
	output->render_area.h = box->p2.y - box->p1.y;
}

/*
 * Uninitialize an output
 *
 * Removes the output from the list of enabled outputs if necessary, but
 * does not call the backend's output disable function. The output will no
 * longer be in the list of pending outputs either.
 *
 * All fields of clv_output become uninitialized, i.e. should not be used
 * anymore. The caller can free the memory after this.
 *
 */
void clv_output_release(struct clv_output *output)
{
	struct weston_head *head, *tmp;

	if (output->idle_repaint_source) {
		clv_event_source_remove(output->idle_repaint_source);
		output->idle_repaint_source = NULL;
	}

/* TODO check
	if (output->enabled)
		weston_compositor_remove_output(output);
*/

	memset(&output->render_area, 0, sizeof(struct clv_rect));
/* TODO check
	pixman_region32_fini(&output->previous_damage);
*/
	list_del(&output->link);

/* TODO check
	wl_list_for_each_safe(head, tmp, &output->head_list, output_link)
		weston_head_detach(head);
*/
}

/*
 * Adds clv_output object to pending output list.
 *
 * The opposite of this operation is built into clv_output_release().
 */
void clv_compositor_add_pending_output(struct clv_output *output,
				       struct clv_compositor *c)
{
	assert(output->disable);
	assert(output->enable);

	list_del(&output->link);
	list_add_tail(&output->link, &c->pending_outputs);
}

/*
 * Attach a head to an output
 * Attaches the given head to the output.
 */
void clv_output_attach_head(struct clv_output *output, struct clv_head *head)
{
	struct clv_compositor *c = output->c;
	struct clv_head *h;
	s32 find = 0;

	head->output = output;
	head->c = c;
	list_for_each_entry(h, &c->heads, link) {
		if (h == head) {
			find = 1;
			break;
		}
	}
	if (!find)
		list_add_tail(&head->link, &c->heads);
	output->head = head;
}

/*
 * Detach a head from its output
 */
void clv_output_detach_head(struct clv_head *head)
{
	if (!head->output)
		return;

	head->output->head = NULL;
	head->output = NULL;
}

void clv_compositor_stack_plane(struct clv_compositor *c,
				struct clv_plane *plane,
				struct clv_plane *above)
{
	if (above)
		__list_add(&plane->link, above->link.prev, &above->link);
	else
		list_add(&plane->link, &c->planes);
}

static s32 set_drm_caps(struct drm_backend *b)
{
	u64 cap;
	s32 ret;
	
	ret = drmGetCap(b->drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1) {
		drm_info("DRM TIMESTAMP: MONOTONIC");
		b->clk_id = CLOCK_MONOTONIC;
	} else {
		drm_info("DRM TIMESTAMP: REALTIME");
		b->clk_id = CLOCK_REALTIME;
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

static void page_flip_handler(s32 fd, u32 frame, u32 sec, u32 usec,
			      u32 crtc_id, void *data)
{
	drm_debug("[CRTC: %u] page flip processing started", crtc_id);
	drm_debug("[CRTC: %u] page flip processing end", crtc_id);
}

static s32 drm_event_proc(s32 fd, u32 mask, void *data)
{
	struct drm_backend *b = data;
	drmEventContext ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.version = 3;
	ctx.page_flip_handler2 = page_flip_handler;
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

static void drm_mode_retrieve(struct drm_output *output);

static void update_connectors(struct drm_backend *b)
{
	struct drm_output *output;
	struct drm_head *head;

	list_for_each_entry(output, &b->outputs, b_link) {
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
		drm_mode_retrieve(output);
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

}

static void drm_repaint_cancel(struct clv_compositor *c, void *repaint_data)
{

}

static void drm_repaint_flush(struct clv_compositor *c, void *repaint_data)
{

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

static struct drm_encoder *enum_encoder(struct drm_backend *b,
					struct drm_head *head,
					struct drm_encoder *last)
{
	struct drm_encoder *encoder, *p;
	s32 used, i;
	u32 encoder_id;
	drmModeConnectorPtr conn = head->connector;

	encoder = calloc(1, sizeof(*encoder));
	if (!encoder)
		return NULL;

	for (i = 0; i < conn->count_encoders; i++) {
		if (last) {
			if (conn->encoders[i] == last->encoder_id)
				last = NULL;
			continue;
		}
		encoder_id = conn->encoders[i];
		used = 0;
		list_for_each_entry(p, &b->encoders, b_link) {
			if (p->encoder_id == encoder_id) {
				used = 1;
				break;
			}
		}
		if (!used)
			break;
	}

	if (i == conn->count_encoders) {
		free(encoder);
		return NULL;
	}

	encoder->enc = drmModeGetEncoder(b->drm_fd, encoder_id);
	if (!encoder->enc) {
		free(encoder);
		return NULL;
	}

	encoder->encoder_id = encoder_id;
	return encoder;
}

static struct drm_output *enum_crtc(struct drm_backend *b,
				    struct drm_encoder *encoder,
				    struct drm_output *last)
{
	struct drm_output *output, *p;
	s32 used;
	u32 crtc_id, possible_crtcs, pipe;
	drmModeEncoderPtr enc = encoder->enc;

	output = calloc(1, sizeof(*output));
	if (!output)
		return NULL;

	possible_crtcs = enc->possible_crtcs;

	for (pipe = 0; pipe < b->res->count_crtcs; pipe++) {
		if (last) {
			if (last->crtc_id == b->res->crtcs[pipe])
				last = NULL;
			continue;
		}
		if (!(possible_crtcs & (1 << pipe)))
			continue;
		crtc_id = b->res->crtcs[pipe];
		used = 0;
		list_for_each_entry(p, &b->outputs, b_link) {
			if (p->crtc_id == crtc_id) {
				used = 1;
				break;
			}
		}
		if (!used)
			break;
	}
	
	if (pipe == b->res->count_crtcs) {
		free(output);
		return NULL;
	}

	output->crtc = drmModeGetCrtc(b->drm_fd, crtc_id);
	if (!output->crtc) {
		free(output);
		return NULL;
	}

	output->props = drmModeObjectGetProperties(b->drm_fd, crtc_id,
						   DRM_MODE_OBJECT_CRTC);
	if (!output->props) {
		drm_err("cannot get CRTC's property");
		drmModeFreeCrtc(output->crtc);
		free(output);
		return NULL;
	}

	output->prop_active = get_prop_id(b->drm_fd, output->props, "ACTIVE");
	drm_debug("CRTC: 0x%08X ACTIVE: 0x%08X", crtc_id, output->prop_active);
	output->prop_mode_id = get_prop_id(b->drm_fd, output->props, "MODE_ID");
	drm_debug("CRTC: 0x%08X MODE_ID: 0x%08X", crtc_id,
		  output->prop_mode_id);
	output->crtc_id = crtc_id;
	output->index = pipe;

	return output;
}

static struct drm_plane *enum_plane(struct drm_backend *b,
				    struct drm_output *output,
				    enum drm_plane_type type)
{
	struct drm_plane *plane, *p;
	s32 used;
	u32 plane_id, i, plane_type;
	drmModePlanePtr pl;

	if (type != DRM_PRIMARY_PL && type != DRM_OVERLAY_PL
	    && type != DRM_CURSOR_PL)
		return NULL;

	plane = calloc(1, sizeof(*plane));
	if (!plane)
		return NULL;

	for (i = 0; i < b->pres->count_planes; i++) {
		pl = drmModeGetPlane(b->drm_fd, b->pres->planes[i]);
		if (!pl)
			continue;
		if (!(pl->possible_crtcs & (1 << output->index))) {
			drmModeFreePlane(pl);
			pl = NULL;
			continue;
		}
		plane_id = b->pres->planes[i];
		plane->props = drmModeObjectGetProperties(
							b->drm_fd,
							plane_id,
							DRM_MODE_OBJECT_PLANE);
		if (!plane->props) {
			drm_err("cannot get plane's property");
			drmModeFreePlane(pl);
			pl = NULL;
			continue;
		}

		used = 0;
		plane->prop_type = get_prop_id(b->drm_fd, plane->props,
					       "type");
		drm_debug("Plane: 0x%08X type: 0x%08X", plane_id,
			  plane->prop_type);
		(void)get_prop_value(b->drm_fd, plane->props, "type",
				     &plane_type);
		if (plane_type != (u32)type) {
			drmModeFreeObjectProperties(plane->props);
			plane->props = NULL;
			drmModeFreePlane(pl);
			pl = NULL;
			continue;
		}
		plane->prop_crtc_id = get_prop_id(b->drm_fd,
						  plane->props, "CRTC_ID");
			
		drm_debug("Plane: 0x%08X CRTC_ID: 0x%08X", plane_id,
			  plane->prop_crtc_id);
		plane->prop_fb_id = get_prop_id(b->drm_fd, plane->props,
						"FB_ID");
		drm_debug("Plane: 0x%08X FB_ID: 0x%08X", plane_id,
			  plane->prop_fb_id);
		plane->prop_crtc_x = get_prop_id(b->drm_fd, plane->props,
						 "CRTC_X");
		drm_debug("Plane: 0x%08X CRTC_X: 0x%08X", plane_id,
			  plane->prop_crtc_x);
		plane->prop_crtc_y = get_prop_id(b->drm_fd, plane->props,
						 "CRTC_Y");
		drm_debug("Plane: 0x%08X CRTC_Y: 0x%08X", plane_id,
			  plane->prop_crtc_y);
		plane->prop_crtc_w = get_prop_id(b->drm_fd, plane->props,
						 "CRTC_W");
		drm_debug("Plane: 0x%08X CRTC_W: 0x%08X", plane_id,
			  plane->prop_crtc_w);
		plane->prop_crtc_h = get_prop_id(b->drm_fd, plane->props,
						 "CRTC_H");
		drm_debug("Plane: 0x%08X CRTC_H: 0x%08X", plane_id,
			  plane->prop_crtc_h);
		plane->prop_src_x = get_prop_id(b->drm_fd, plane->props,
						"SRC_X");
		drm_debug("Plane: 0x%08X SRC_X: 0x%08X", plane_id,
			  plane->prop_src_x);
		plane->prop_src_y = get_prop_id(b->drm_fd, plane->props,
						"SRC_Y");
		drm_debug("Plane: 0x%08X SRC_Y: 0x%08X", plane_id,
			  plane->prop_src_y);
		plane->prop_src_w = get_prop_id(b->drm_fd, plane->props,
						"SRC_W");
		drm_debug("Plane: 0x%08X SRC_W: 0x%08X", plane_id,
			  plane->prop_src_w);
		plane->prop_src_h = get_prop_id(b->drm_fd, plane->props,
						"SRC_H");
		drm_debug("Plane: 0x%08X SRC_H: 0x%08X", plane_id,
			  plane->prop_src_h);
		
		list_for_each_entry(p, &b->planes, b_link) {
			if (p->plane_id == plane_id) {
				drmModeFreeObjectProperties(plane->props);
				plane->props = NULL;
				drmModeFreePlane(pl);
				pl = NULL;
				used = 1;
				break;
			}
		}
		if (!used)
			break;
	}

	if (i == b->pres->count_planes) {
		if (plane->props) {
			drmModeFreeObjectProperties(plane->props);
			plane->props = NULL;
		}
		if (pl) {
			drmModeFreePlane(pl);
			pl = NULL;
		}
		free(plane);
		plane = NULL;
		return NULL;
	}

	plane->pl = pl;
	plane->plane_id = plane_id;

	return plane;
}

static void release_encoder(struct drm_encoder *encoder)
{
	if (!encoder)
		return;

	if (encoder->enc) {
		drmModeFreeEncoder(encoder->enc);
		encoder->enc = NULL;
	}

	list_del(&encoder->b_link);
	free(encoder);
}

static void release_crtc(struct drm_output *output)
{
	if (!output)
		return;

	if (output->crtc) {
		drmModeFreeCrtc(output->crtc);
		output->crtc = NULL;
	}
	list_del(&output->b_link);
	if (output->props) {
		drmModeFreeObjectProperties(output->props);
		output->props = NULL;
	}
	free(output);
}

static void release_connector(struct drm_head *head)
{
	if (!head)
		return;

	if (head->connector) {
		drmModeFreeConnector(head->connector);
		head->connector = NULL;
	}

	list_del(&head->b_link);

	if (head->props) {
		drmModeFreeObjectProperties(head->props);
		head->props = NULL;
	}

	free(head);
}

static void release_plane(struct drm_plane *plane)
{
	if (!plane)
		return;

	if (plane->pl) {
		drmModeFreePlane(plane->pl);
		plane->pl = NULL;
	}

	list_del(&plane->b_link);

	if (plane->props) {
		drmModeFreeObjectProperties(plane->props);
		plane->props = NULL;
	}

	free(plane);
}

static s32 drm_fb_addfb(struct drm_backend *b, struct drm_fb *fb)
{
	return drmModeAddFB2(fb->fd, fb->w, fb->h, fb->drm_fmt,
			     fb->handles, fb->strides, fb->offsets, &fb->fb_id,
			     0);
}

static void drm_fb_destroy(struct drm_fb *fb)
{
	if (fb->fb_id != 0)
		drmModeRmFB(fb->fd, fb->fb_id);
	/* TODO check weston_buffer_reference(&fb->buffer_ref, NULL);
	weston_buffer_release_reference(&fb->buffer_release_ref, NULL);*/
	free(fb);
}

static void drm_fb_destroy_gbm(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;

	assert(fb->type == DRM_BUF_GBM_SURFACE || fb->type == DRM_BUF_CURSOR);
	drm_fb_destroy(fb);
}

static struct drm_fb *drm_fb_ref(struct drm_fb *fb)
{
	fb->refcnt++;

	return fb;
}

static struct drm_fb *drm_fb_get_from_bo(struct gbm_bo *bo,
					 struct drm_backend *b,
					 s32 is_opaque, enum drm_fb_type type)
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

	/* We can scanout an ARGB buffer if the surface's opaque region covers
	 * the whole output, but we have to use XRGB as the KMS format code. */
	if (is_opaque) {
		if (fb->drm_fmt == DRM_FORMAT_ARGB8888) {
			drm_info("change ARGB8888 to XRGB8888");
			fb->drm_fmt = DRM_FORMAT_XRGB8888;
		}
	}

	if (drm_fb_addfb(b, fb) != 0) {
		if (type == DRM_BUF_GBM_SURFACE)
			drm_err("failed to create kms fb: %s", strerror(errno));
		goto error;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_gbm);

	return fb;

error:
	free(fb);
	return NULL;
}

static void drm_fb_destroy_dmabuf(struct drm_fb *fb)
{
	/* We deliberately do not close the GEM handles here;
	 * GBM manages their lifetime through the BO. */
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

static void drm_output_destroy(struct clv_output *output)
{
	struct drm_output *o;
	struct drm_plane *primary_plane, *cursor_plane, *overlay_plane;
	struct drm_encoder *encoder;
	struct drm_head *head;

	o = container_of(output, struct drm_output, base);
	assert(o);
	primary_plane = o->primary_plane;
	overlay_plane = o->overlay_plane;
	cursor_plane = o->cursor_plane;
	encoder = o->encoder;
	head = o->head;
	release_encoder(encoder);
	release_connector(head);
	release_plane(primary_plane);
	if (overlay_plane)
		release_plane(overlay_plane);
	release_plane(cursor_plane);
	release_crtc(o);
}

static void drm_output_init(struct drm_output *output, struct clv_compositor *c)
{
	output->gbm_bo_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
	clv_output_init(&output->base, c, output->index);
	output->disable_pending = 0;
	output->dpms_off_pending = 0;
	output->atomic_complete_pending = 0;
	output->state_cur = drm_output_state_alloc(output, NULL);
	clv_compositor_add_pending_output(&output->base, c);
}

static void drm_output_fini_cursor_egl(struct drm_output *output)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(output->gbm_cursor_fb); i++) {
		drm_fb_unref(output->gbm_cursor_fb[i]);
		output->gbm_cursor_fb[i] = NULL;
	}
}

static int
drm_output_init_cursor_egl(struct drm_output *output, struct drm_backend *b)
{
	u32 i;
	struct gbm_bo *bo;

	for (i = 0; i < ARRAY_SIZE(output->gbm_cursor_fb); i++) {
		bo = gbm_bo_create(b->gbm, b->cursor_w, b->cursor_h,
				   GBM_FORMAT_ARGB8888,
				   GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
		if (!bo)
			goto error;

		output->gbm_cursor_fb[i] = drm_fb_get_from_bo(bo, b, 0,
							      DRM_BUF_CURSOR);
		if (!output->gbm_cursor_fb[i]) {
			gbm_bo_destroy(bo);
			goto error;
		}
	}

	return 0;

error:
	drm_err("cursor buffers unavailable");
	drm_output_fini_cursor_egl(output);
	return -1;
}

static void drm_output_fini_egl(struct drm_output *output)
{
	struct drm_backend *b = container_of(output->base.c->backend,
					     struct drm_backend, base);

	/* Destroying the GBM surface will destroy all our GBM buffers,
	 * regardless of refcount. Ensure we destroy them here. */
	if (output->primary_plane->state_cur->fb &&
	    output->primary_plane->state_cur->fb->type == DRM_BUF_GBM_SURFACE) {
		drm_plane_state_free(output->primary_plane->state_cur, 1);
		output->primary_plane->state_cur =
			drm_plane_state_alloc(output->primary_plane, NULL);
		output->primary_plane->state_cur->complete = 1;
	}

	renderer->output_destroy(&output->base);
	gbm_surface_destroy(output->gbm_surface);
	output->gbm_surface = NULL;
	drm_output_fini_cursor_egl(output);
}

static void drm_output_deinit(struct clv_output *base)
{
	struct drm_output *output = container_of(base, struct drm_output, base);
	struct drm_backend *b = container_of(base->c->backend,
					     struct drm_backend, base);

	drm_output_fini_egl(output);

	/* Since our planes are no longer in use anywhere, remove their base
	 * weston_plane's link from the plane stacking list, unless we're
	 * shutting down, in which case the plane has already been
	 * destroyed. */
	list_del(&output->primary_plane->base.link);
	INIT_LIST_HEAD(&output->primary_plane->base.link);

	if (output->cursor_plane) {
		list_del(&output->cursor_plane->base.link);
		INIT_LIST_HEAD(&output->cursor_plane->base.link);
		/* Turn off hardware cursor */
		drmModeSetCursor(b->drm_fd, output->crtc_id, 0, 0, 0);
	}
}

static s32 drm_output_init_egl(struct drm_output *output, struct drm_backend *b)
{
	EGLint format[2] = {
		b->gbm_format,
		0,
	};
	struct clv_mode *mode = output->base.current_mode;
	s32 vid;

	if (!mode) {
		/* select preferred mode */
		list_for_each_entry(mode, &output->base.modes, link) {
			if (mode->flags & CLV_OUTPUT_MODE_PREFERRED) {
				output->base.current_mode = mode;
				mode->flags |= CLV_OUTPUT_MODE_CURRENT;
				break;
			}
		}
	}
	assert(output->gbm_surface == NULL);
	output->gbm_surface = gbm_surface_create(b->gbm, mode->w, mode->h,
						 b->gbm_format,
						 output->gbm_bo_flags);

	if (!output->gbm_surface) {
		gbm_err("failed to create gbm surface.");
		return -1;
	}

	if (renderer->output_create(&output->base,
				    (EGLNativeWindowType)output->gbm_surface,
				    output->gbm_surface,
				    format,
				    1, &vid) < 0) {
		drm_err("failed to create gl renderer output state");
		gbm_surface_destroy(output->gbm_surface);
		output->gbm_surface = NULL;
		return -1;
	}

	drm_output_init_cursor_egl(output, b);

	return 0;
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

static struct drm_mode *drm_output_add_mode(struct drm_output *output,
					    const drmModeModeInfo *info)
{
	struct drm_mode *mode;

	mode = calloc(1, sizeof(*mode));
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

	list_add_tail(&mode->base.link, &output->base.modes);

	return mode;
}

static void drm_mode_retrieve(struct drm_output *output)
{
	struct drm_head *head = output->head;
	struct clv_mode *m, *next;
	struct drm_mode *dm;
	s32 i;

	if (!head)
		return;

	list_for_each_entry_safe(m, next, &output->base.modes, link) {
		dm = container_of(m, struct drm_mode, base);
		free(dm);
		list_del(&m->link);
	}

	if (head->connector->connection == DRM_MODE_CONNECTED) {
		drm_debug("connector is connected");
		head->base.connected = 1;
		for (i = 0; i < head->connector->count_modes; i++) {
			drm_output_add_mode(output, &head->connector->modes[i]);
		}
	} else {
		drm_debug("connector is not connected");
		head->base.connected = 0;
	}
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
	struct drm_output *output = container_of(output_base,
						 struct drm_output, base);
	struct drm_backend *b = container_of(output_base->c->backend,
					     struct drm_backend, base);
/* TODO
	struct drm_mode *drm_mode = choose_mode(output, mode);
*/
	struct drm_mode *drm_mode = container_of(mode, struct drm_mode, base);

	if (!drm_mode) {
		drm_err("output-%u: invalid resolution %dx%d",
			   output_base->index, mode->w, mode->h);
		return -1;
	}

	if (&drm_mode->base == output->base.current_mode)
		return 0;

	output->base.current_mode->flags = 0;

	output->base.current_mode = &drm_mode->base;
	output->base.current_mode->flags =
		CLV_OUTPUT_MODE_CURRENT | CLV_OUTPUT_MODE_PREFERRED;

	/* XXX: This drops our current buffer too early, before we've started
	 *      displaying it. Ideally this should be much more atomic and
	 *      integrated with a full repaint cycle, rather than doing a
	 *      sledgehammer modeswitch first, and only later showing new
	 *      content.
	 */

	drm_output_fini_egl(output);
	if (drm_output_init_egl(output, b) < 0) {
		drm_err("failed to init output egl state with new mode");
		return -1;
	}

	return 0;
}

static void drm_output_start_repaint_loop(struct clv_output *output_base)
{
	
}

static s32 drm_output_repaint(struct clv_output *output_base,
			      void *repaint_data)
{

}

static void drm_assign_planes(struct clv_output *output_base,
			      void *repaint_data)
{

}

static void drm_set_dpms(struct clv_output *output_base, enum clv_dpms level)
{

}

static s32 drm_output_enable(struct clv_output *base)
{
	struct drm_output *output = container_of(base, struct drm_output, base);
	struct drm_backend *b = container_of(base->c->backend,
					     struct drm_backend, base);

	if (drm_output_init_egl(output, b) < 0) {
		drm_err("Failed to init output gl state");
		goto err;
	}

	output->base.set_dpms = drm_set_dpms;
	output->base.start_repaint_loop = drm_output_start_repaint_loop;
	output->base.repaint = drm_output_repaint;
	output->base.assign_planes = drm_assign_planes;
	output->base.switch_mode = drm_output_switch_mode;

	clv_compositor_stack_plane(b->c, &output->cursor_plane->base, NULL);

	clv_compositor_stack_plane(b->c, &output->primary_plane->base,
				   &b->c->root_plane);

	drm_debug("Output %u (crtc %d) video modes:", output->base.index,
		  output->crtc_id);
	drm_output_print_modes(output);
	base->enabled = 1;

	return 0;

err:
	return -1;
}

static s32 drm_output_disable(struct clv_output *base)
{
	struct drm_output *output = container_of(base, struct drm_output, base);

	if (output->atomic_complete_pending) {
		output->disable_pending = 1;
		return -1;
	}

	drm_info("Disabling output %u", output->base.index);

	if (output->base.enabled)
		drm_output_deinit(&output->base);

	output->disable_pending = 0;

	base->enabled = 0;

	return 0;
}

static struct clv_output *drm_output_create(struct clv_compositor *c,
					    u32 head_index)
{
	struct drm_head *head;
	struct drm_output *output, *last_output;
	struct drm_encoder *encoder, *last_encoder;
	struct drm_plane *primary_plane, *cursor_plane, *overlay_plane;
	struct drm_backend *b = container_of(c->backend, struct drm_backend,
					     base);
	struct clv_box *boxes;
	s32 n, vid;

	if (!renderer) {
		drm_warn("renderer is not created.");
		renderer_create(c, &b->gbm_format, 1, 1, b->gbm, &vid);
		assert(renderer = c->renderer);
	}

	if (head_index >= b->res->count_connectors) {
		drm_err("head_index %u out of range.[0-%u]", head_index,
			b->res->count_connectors - 1);
		goto err1;
	}

	head = calloc(1, sizeof(*head));
	if (!head)
		goto err1;

	
	head->b = b;
	head->index = head_index;
	head->connector_id = b->res->connectors[head_index];
	head->connector = drmModeGetConnector(b->drm_fd,
					      b->res->connectors[head_index]);
	if (!head->connector)
		goto err2;
	head->props = drmModeObjectGetProperties(b->drm_fd,
						 head->connector_id,
						 DRM_MODE_OBJECT_CONNECTOR);
	if (!head->props)
		goto err3;

	head->prop_crtc_id = get_prop_id(b->drm_fd, head->props, "CRTC_ID");
	drm_debug("Connector: %u CRTC_ID %u", head->connector_id,
		  head->prop_crtc_id);
	head->prop_dpms = get_prop_id(b->drm_fd, head->props, "DPMS");
	drm_debug("Connector: %u DPMS %u", head->connector_id,
		  head->prop_dpms);

	last_encoder = NULL;
find_encoder:
	encoder = enum_encoder(b, head, last_encoder);
	if (!encoder) {
		if (last_encoder) {
			if (last_encoder->enc)
				drmModeFreeEncoder(last_encoder->enc);
			free(last_encoder);
		}
		goto err4;
	}
	drm_debug("enumerate encoder %u", encoder->encoder_id);
	last_encoder = encoder;

	last_output = NULL;
find_crtc:
	output = enum_crtc(b, encoder, last_output);
	if (!output) {
		if (last_output) {
			if (last_output->crtc)
				drmModeFreeCrtc(last_output->crtc);
			if (last_output->props)
				drmModeFreeObjectProperties(last_output->props);
			free(last_output);
		}
		drm_debug("re-enumerate encoder");
		goto find_encoder;
	}
	drm_debug("enumerate crtc %u", output->crtc_id);
	last_output = output;

	primary_plane = enum_plane(b, output, DRM_PRIMARY_PL);
	if (!primary_plane) {
		goto find_crtc;
	}
	drm_debug("enumerate primary plane %u", primary_plane->plane_id);

	overlay_plane = enum_plane(b, output, DRM_OVERLAY_PL);
	if (!overlay_plane) {
		drm_warn("cannot enumerate overlay plane");
	} else {
		drm_debug("enumerate overlay plane %u",overlay_plane->plane_id);
	}

	cursor_plane = enum_plane(b, output, DRM_CURSOR_PL);
	if (!cursor_plane) {
		if (overlay_plane) {
			if (overlay_plane->pl)
				drmModeFreePlane(overlay_plane->pl);
			if (overlay_plane->props)
				drmModeFreeObjectProperties(
					overlay_plane->props);
			free(overlay_plane);
		}
		if (primary_plane) {
			if (primary_plane->pl)
				drmModeFreePlane(primary_plane->pl);
			if (primary_plane->props)
				drmModeFreeObjectProperties(
					primary_plane->props);
			free(primary_plane);
		}
		goto find_crtc;
	}
	drm_debug("enumerate cursor plane %u", cursor_plane->plane_id);

	output->b = b;
	primary_plane->b = b;
	if (overlay_plane)
		overlay_plane->b = b;
	cursor_plane->b = b;
	encoder->b = b;
	head->b = b;

	output->primary_plane = primary_plane;
	output->overlay_plane = overlay_plane;
	output->cursor_plane = cursor_plane;
	output->head = head;
	output->encoder = encoder;
	list_add_tail(&output->b_link, &b->outputs);

	encoder->output = output;
	encoder->head = head;
	list_add_tail(&encoder->b_link, &b->encoders);

	head->output = output;
	head->encoder = encoder;
	list_add_tail(&head->b_link, &b->heads);

	primary_plane->output = output;
	list_add_tail(&primary_plane->b_link, &b->planes);

	if (overlay_plane) {
		overlay_plane->output = output;
		list_add_tail(&overlay_plane->b_link, &b->planes);
	}

	cursor_plane->output = output;
	list_add_tail(&cursor_plane->b_link, &b->planes);

	drm_debug("Head index %u >>", head->index);
	drm_debug("\tConnector ID->Encoder ID->CRTC ID->Primary Plane ID/"
		  "Cursor Plane ID/Overlay Plane ID");
	drm_debug("\t0x%08X->0x%08X->0x%08X->0x%08X/0x%08X/0x%08X",
		head->connector_id, head->encoder->encoder_id,
		head->encoder->output->crtc_id,
		head->encoder->output->primary_plane->plane_id,
		head->encoder->output->cursor_plane->plane_id,
		head->encoder->output->overlay_plane ? 
		    head->encoder->output->overlay_plane->plane_id: (u32)(-1));

	output->base.destroy = drm_output_destroy;
	output->base.enable = drm_output_enable;
	output->base.disable = drm_output_disable;

	drm_output_init(output, c);
	clv_output_attach_head(&output->base, &head->base);
	boxes = clv_region_boxes(&b->canvas, &n);
	clv_output_init_area(&output->base, &boxes[head->index]);
	drm_mode_retrieve(output);
	if (output->head->base.connected) {
		drm_output_enable(&output->base);
	}

	return &output->base;
	
err4:
	if (head && head->props) {
		drmModeFreeObjectProperties(head->props);
		head->props = NULL;
	}
err3:
	if (head && head->connector) {
		drmModeFreeConnector(head->connector);
		head->connector = NULL;
	}
err2:
	if (head) {
		free(head);
		head = NULL;
	}
err1:
	return NULL;
}

static void drm_update_config(struct clv_compositor *c,
			      struct clv_compositor_config *config)
{

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

struct clv_backend *drm_backend_create(struct clv_compositor *c,
				       struct clv_compositor_config *config)
{
	struct drm_backend *b;

	renderer = c->renderer;

	b = calloc(1, sizeof(*b));
	if (!b)
		return NULL;

	c->backend = &b->base;

	b->c = c;

	b->mode = config->mode;
	clv_region_init(&b->canvas);
	clv_region_copy(&b->canvas, &config->canvas);

	strcpy(b->dev_node, config->dev_node);
	b->drm_fd = open(config->dev_node, O_RDWR | O_CLOEXEC, 0644);
	if (b->drm_fd < 0) {
		drm_err("failed to open %s. %s", config->dev_node,
			strerror(errno));
		goto error;
	}

	if (set_drm_caps(b) < 0) {
		drm_err("failed to set DRM capabilities");
		goto error;
	}
	/* set clock type */
	c->presentation_clock = b->clk_id;

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

	b->loop = clv_server_get_event_loop(c->server);

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

	INIT_LIST_HEAD(&b->planes);
	INIT_LIST_HEAD(&b->encoders);
	INIT_LIST_HEAD(&b->heads);
	INIT_LIST_HEAD(&b->outputs);

	b->base.destroy = drm_backend_destroy;
	b->base.repaint_begin = drm_repaint_begin;
	b->base.repaint_flush = drm_repaint_flush;
	b->base.repaint_cancel = drm_repaint_cancel;
	b->base.output_create = drm_output_create;

	for (s32 i = 0; i < b->res->count_connectors; i++) {
		drm_output_create(c, i);
	}

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

