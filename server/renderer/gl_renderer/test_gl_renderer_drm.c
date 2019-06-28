#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_compositor.h>
#include <clover_region.h>

struct drm_fb {
	u32 fb_id;
	u32 stride;
	u32 handle;
	u32 size;
	s32 width, height;
	s32 fd;

	struct gbm_bo *bo;
};

struct window {
	struct clv_surface surface;
	struct clv_view view;
	struct shm_buffer buffer;
	s32 x, y;
	u32 w, h;
	char shm_name[128];
};

struct display {
	struct clv_compositor compositor;
	struct clv_output output;
	struct drm_fb *current, *next;
	struct window background;
	struct window client_win;
	s32 drm_fd;
	u32 gbm_fmt;
	u32 plane_id;
	u32 connector_id;
	u32 crtc_id;
	u32 pipe;
	struct gbm_device *gbm;
	struct gbm_surface *gbm_surface;
	drmModeRes *res;
	drmModePlaneRes *pres;
	drmModeCrtcPtr crtc;
	drmModeConnectorPtr conn;
	drmModeEncoderPtr enc;
	drmModePlanePtr plane;
	drmModeModeInfo mode;
	s32 initialed;
	pthread_mutex_t page_flip_lock;
	fd_set fds;
} g_disp;

static void drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;

	if (fb->fb_id)
		drmModeRmFB(fb->fd, fb->fb_id);

	free(data);
}

static void release_fb(struct display *disp, struct drm_fb *fb)
{
	if (!fb)
		return;

	if (fb->bo) {
		gbm_surface_release_buffer(disp->gbm_surface, fb->bo);
	}
}

s32 init_display(struct display *disp)
{
	u32 encoder_id;
	u32 possible_crtcs;
	s32 i, j, vid;
	struct clv_compositor *c = &disp->compositor;
	struct clv_output *output = &disp->output;
	struct window *background = &disp->background;
	struct window *client_win = &disp->client_win;
	u8 *data;
	u32 *pixel;
	u32 w, h;

	unlink("/dev/shm/testshm0");
	unlink("/dev/shm/testshm1");

	disp->next = disp->current = NULL;
	pthread_mutex_init(&disp->page_flip_lock, NULL);
	disp->drm_fd = open("/dev/dri/card0", O_RDWR, 0644);
	disp->res = drmModeGetResources(disp->drm_fd);
	disp->pres = drmModeGetPlaneResources(disp->drm_fd);
	drmSetClientCap(disp->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	disp->conn = drmModeGetConnector(disp->drm_fd, disp->res->connectors[0]);
	if (disp->conn->connection == DRM_MODE_DISCONNECTED) {
		printf("connector disconnected.\n");
		return -1;
	} else if (disp->conn->connection == DRM_MODE_CONNECTED) {
		printf("connector connected.\n");
	}
	disp->connector_id = disp->res->connectors[0];
	encoder_id = disp->conn->encoders[0];
	disp->enc = drmModeGetEncoder(disp->drm_fd, encoder_id);
	possible_crtcs = disp->enc->possible_crtcs;
	for (i = 0; i < disp->res->count_crtcs; i++) {
		if (possible_crtcs & (1 << i))
			break;
	}
	if (i == disp->res->count_crtcs)
		return -1;
	disp->crtc = drmModeGetCrtc(disp->drm_fd, disp->res->crtcs[i]);
	disp->crtc_id = disp->res->crtcs[i];
	disp->pipe = i;
	for (j = 0; j < disp->pres->count_planes; j++) {
		disp->plane = drmModeGetPlane(disp->drm_fd, disp->pres->planes[j]);
		if (disp->plane->possible_crtcs & (1 << i))
			break;
	}
	if (j == disp->pres->count_planes)
		return -1;
	disp->plane_id = disp->pres->planes[j];
	memcpy(&disp->mode, &disp->conn->modes[0], sizeof(disp->mode));
	disp->gbm = gbm_create_device(disp->drm_fd);
	assert(disp->gbm);
	disp->gbm_surface = gbm_surface_create(disp->gbm,
		disp->mode.hdisplay, disp->mode.vdisplay,
		GBM_FORMAT_ARGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	assert(disp->gbm_surface);
	disp->initialed = 0;
	disp->gbm_fmt = GBM_FORMAT_ARGB8888;

	w = disp->mode.hdisplay;
	h = disp->mode.vdisplay;

	memset(c, 0, sizeof(*c));
	INIT_LIST_HEAD(&c->views);
	INIT_LIST_HEAD(&c->outputs);

	memset(output, 0, sizeof(*output));
	output->c = c;
	output->render_area.pos.x = 0;
	output->render_area.pos.y = 0;
	output->render_area.w = w;
	output->render_area.h = h;
	list_add_tail(&output->link, &c->outputs);

	memset(&background->surface, 0, sizeof(struct clv_surface));
	background->surface.c = c;
	background->surface.is_opaque = 0;
	background->surface.view = &background->view;
	background->x = 0;
	background->y = 0;
	background->w = w;
	background->h = h;
	clv_region_init_rect(&background->surface.damage, 0, 0,
				background->w, background->h);
	background->surface.w = background->w;
	background->surface.h = background->h;
	clv_region_init_rect(&background->surface.opaque, 0, 0,
				background->w, background->h);
	clv_signal_init(&background->surface.destroy_signal);

	memset(&client_win->surface, 0, sizeof(struct clv_surface));
	client_win->surface.c = c;
	client_win->surface.is_opaque = 0;
	client_win->surface.view = &client_win->view;
	client_win->x = 0;
	client_win->y = 0;
	client_win->w = w / 4;
	client_win->h = h / 4;
	clv_region_init_rect(&client_win->surface.damage, 0, 0,
				client_win->w, client_win->h);
	client_win->surface.w = client_win->w;
	client_win->surface.h = client_win->h;
	clv_region_init_rect(&client_win->surface.opaque, 0, 0,
				client_win->w, client_win->h);
	clv_signal_init(&client_win->surface.destroy_signal);

	memset(&background->view, 0, sizeof(struct clv_view));
	background->view.surface = &background->surface;
	background->view.type = CLV_VIEW_TYPE_PRIMARY;
	background->view.area.pos.x = 0;
	background->view.area.pos.y = 0;
	background->view.area.w = background->w;
	background->view.area.h = background->h;
	background->view.alpha = 1.0f;
	list_add_tail(&background->view.link, &c->views);

	memset(&client_win->view, 0, sizeof(struct clv_view));
	client_win->view.surface = &client_win->surface;
	client_win->view.type = CLV_VIEW_TYPE_PRIMARY;
	client_win->view.area.pos.x = 0;
	client_win->view.area.pos.y = 0;
	client_win->view.area.w = client_win->w;
	client_win->view.area.h = client_win->h;
	client_win->view.alpha = 1.0f;
	list_add_tail(&client_win->view.link, &c->views);

	memset(&background->buffer, 0, sizeof(struct shm_buffer));
	background->buffer.base.type = CLV_BUF_TYPE_SHM;
	background->buffer.base.w = background->w;
	background->buffer.base.h = background->h;
	background->buffer.base.stride = background->w * 4;
	background->buffer.base.size = background->w * background->h * 4;
	background->buffer.base.pixel_fmt = CLV_PIXEL_FMT_ARGB8888;
	background->buffer.base.count_planes = 1;
	sprintf(background->buffer.base.name, "testshm%d", 0);
	clv_shm_init(&background->buffer.shm, background->buffer.base.name,
			background->buffer.base.size, 1);
	data = background->buffer.shm.map;
	pixel = (u32 *)data;
	for (j = 0; j < background->buffer.base.size / 4; j++)
		pixel[j] = 0xFF0000FF;
	
	memset(&client_win->buffer, 0, sizeof(struct shm_buffer));
	client_win->buffer.base.type = CLV_BUF_TYPE_SHM;
	client_win->buffer.base.w = client_win->w;
	client_win->buffer.base.h = client_win->h;
	client_win->buffer.base.stride = client_win->w * 4;
	client_win->buffer.base.size = client_win->w * client_win->h * 4;
	client_win->buffer.base.pixel_fmt = CLV_PIXEL_FMT_ARGB8888;
	client_win->buffer.base.count_planes = 1;
	sprintf(client_win->buffer.base.name, "testshm%d", 1);
	clv_shm_init(&client_win->buffer.shm, client_win->buffer.base.name,
			client_win->buffer.base.size, 1);
	data = client_win->buffer.shm.map;
	pixel = (u32 *)data;
	for (j = 0; j < client_win->buffer.base.size / 4; j++)
		pixel[j] = 0xFFFFFF00;
	
	assert(renderer_create(c, &disp->gbm_fmt, 1, 1, disp->gbm, &vid) == 0);
	printf("vid = %u\n", vid);
//	set_renderer_dbg(0x33);
	assert(c->renderer->output_create(output, NULL, disp->gbm_surface,
		&disp->gbm_fmt, 1, &vid) == 0);

	printf("init complete.\n");
	return 0;
}

static struct drm_fb *drm_fb_get_from_bo(
	struct gbm_bo *bo,
	struct display *disp
)
{
	struct drm_fb *fb = gbm_bo_get_user_data(bo);
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	int32_t ret;

	if (fb)
		return fb;

	fb = calloc(1, sizeof *fb);
	if (fb == NULL) {
		clv_err("cannot alloc fb");
		return NULL;
	}

	fb->bo = bo;

	fb->width = gbm_bo_get_width(bo);
	fb->height = gbm_bo_get_height(bo);
	fb->stride = gbm_bo_get_stride(bo);
	fb->handle = gbm_bo_get_handle(bo).u32;
	fb->size = fb->stride * fb->height;
	fb->fd = disp->drm_fd;

	ret = -1;

	handles[0] = fb->handle;
	pitches[0] = fb->stride;
	offsets[0] = 0;
	ret = drmModeAddFB2(disp->drm_fd, fb->width, fb->height,
			    disp->gbm_fmt, handles, pitches, offsets,
			    &fb->fb_id, 0);
	if (ret) {
		clv_err("addfb2 failed: %m");
	}

	if (ret)
		ret = drmModeAddFB(disp->drm_fd, fb->width, fb->height,
				   24, 32, fb->stride, fb->handle, &fb->fb_id);

	if (ret) {
		clv_err("failed to create kms fb: %m");
		free(fb);
		return NULL;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

	return fb;
}

static u32 drm_waitvblank_pipe(u32 pipe)
{
	if (pipe > 1)
		return (pipe << DRM_VBLANK_HIGH_CRTC_SHIFT) &
				DRM_VBLANK_HIGH_CRTC_MASK;
	else if (pipe > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static void vblank_handler(
	s32 fd,
	u32 frame,
	u32 sec,
	u32 usec,
	void *data
)
{
	struct display *disp = data;

	if (disp->current)
		release_fb(disp, disp->current);
	disp->current = disp->next;
	disp->next = NULL;

	pthread_mutex_unlock(&disp->page_flip_lock);
}

static void *drm_select_thread(void *data)
{
	int32_t rt;
	struct display *disp = data;
	struct timeval tv;
	drmEventContext evctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.vblank_handler = vblank_handler,
	};
	
	while (1) {
		FD_ZERO(&disp->fds);
		FD_SET(disp->drm_fd, &disp->fds);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		rt = select(disp->drm_fd + 1, &disp->fds, NULL, NULL, &tv);
		if (rt < 0) {
			clv_err("select err: %s", strerror(errno));
			break;
		} else if (rt == 0) {
		} else {
			if (FD_ISSET(disp->drm_fd, &disp->fds)) {
				drmHandleEvent(disp->drm_fd, &evctx);
			}
		}
	}
	return NULL;
}

void draw(struct display *disp)
{
	struct clv_compositor *c = &disp->compositor;
	struct clv_output *output = &disp->output;
	struct gbm_bo *bo;
	drmVBlank vbl = {
		.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT,
		.request.sequence = 1,
	};
	struct window *background = &disp->background;
	struct window *client_win = &disp->client_win;
	struct clv_surface *surf_background = &background->surface;
	struct clv_surface *surf_client_win = &client_win->surface;
	struct clv_view *view_background = &background->view;
	struct clv_view *view_client_win = &client_win->view;
	struct shm_buffer *buf_background = &background->buffer;
	struct shm_buffer *buf_client_win = &client_win->buffer;
	u8 *data;
	u32 *pixel, color_background = 0xFF0000FF, color_client_win = 0xFFFFFF00;
	s32 i;
	s32 x = 0, y = 0;
	s32 delta_x = 1;
	s32 delta_y = 1;

#if 0
	c->renderer->attach_buffer(surf_background,
				   &buf_background->base);
	c->renderer->attach_buffer(surf_client_win,
				   &buf_client_win->base);
	c->renderer->flush_damage(surf_background);
	c->renderer->flush_damage(surf_client_win);
#endif
	do {
#if 1
		data = buf_background->shm.map;
		pixel = (u32 *)data;
		if (color_background == 0xFF000000)
			color_background = 0xFF0000FF;
		else
			color_background--;
		for (i = 0; i < buf_background->base.size / 4; i++)
			pixel[i] = color_background;
		clv_region_fini(&surf_background->damage);
		clv_region_init_rect(&surf_background->damage,
				surf_background->w/10, surf_background->h/10,
				surf_background->w/5*4, surf_background->h/5*4);
		c->renderer->attach_buffer(surf_background,
					   &buf_background->base);
		c->renderer->flush_damage(surf_background);

		data = buf_client_win->shm.map;
		pixel = (u32 *)data;
		if (color_client_win == 0xFF000000)
			color_client_win = 0xFFFFFF00;
		else
			color_client_win -= 0xFF000100;
		for (i = 0; i < buf_client_win->base.size / 4; i++)
			pixel[i] = color_client_win;
		clv_region_fini(&surf_client_win->damage);
		clv_region_init_rect(&surf_client_win->damage,
				surf_client_win->w/4, surf_client_win->h/4,
				surf_client_win->w/2, surf_client_win->h/2);
		c->renderer->attach_buffer(surf_client_win,
					   &buf_client_win->base);
		c->renderer->flush_damage(surf_client_win);
#endif
#if 0
		clv_region_fini(&surf_background->damage);
		clv_region_init_rect(&surf_background->damage, 0, 0, 10, 10);
		c->renderer->flush_damage(surf_background);
#endif
		view_client_win->area.pos.x = x;
		view_client_win->area.pos.y = y;

		c->renderer->repaint_output(output);

		x += delta_x;
		y += delta_y;
		if ((x + client_win->w) > background->w) {
			delta_x = -1;
		} else if (x < 0) {
			delta_x = 1;
		}
		if ((y + client_win->h) > background->h) {
			delta_y = -1;
		} else if (y < 0) {
			delta_y = 1;
		}

		pthread_mutex_lock(&disp->page_flip_lock);
		bo = gbm_surface_lock_front_buffer(disp->gbm_surface);
		disp->next = drm_fb_get_from_bo(bo, disp);
		assert(disp->next);
		
		if (!disp->initialed) {
			if (drmModeSetCrtc(disp->drm_fd, disp->crtc_id,
				     disp->next->fb_id,
				     0, 0,
				     &disp->connector_id, 1,
				     &disp->mode) < 0) {
				printf("%u %u %u %u %u\n", disp->crtc_id,
					disp->next->fb_id, disp->connector_id,
					disp->mode.hdisplay, disp->mode.vdisplay);
				printf("SetCrtc failed %m\n");
			}
			disp->initialed = 1;
			printf("SetCrtc !!!!!!!!!!!!!\n");
		}
		drmModeSetPlane(disp->drm_fd, disp->plane_id, disp->crtc_id,
		    disp->next->fb_id, 0, 0, 0, disp->mode.hdisplay,
		    disp->mode.vdisplay, 0, 0, (background->w << 16),
		    (background->h << 16));

		vbl.request.type |= drm_waitvblank_pipe(disp->pipe);
		/*
		 * Queue a vblank signal so we know when the surface
		 * becomes active on the display or has been replaced.
		 */
		vbl.request.signal = (u64)disp;
		if (drmWaitVBlank(disp->drm_fd, &vbl) < 0) {
			clv_err("queueing vblank failed: %m");
			clv_debug("crtc %08X %08X", disp->crtc_id,
			    disp->next->fb_id);
		}
	} while (1);
}

s32 main(s32 argc, char **argv)
{
	pthread_t tid;

	init_display(&g_disp);
	pthread_create(&tid, NULL, drm_select_thread, &g_disp);
	draw(&g_disp);
	
	return 0;
}

