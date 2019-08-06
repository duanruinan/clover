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
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_ipc.h>
#include <clover_shm.h>
#include <clover_event.h>
#include <clover_protocal.h>

u32 frame_cnt = 0;

void usage(void)
{
	printf("clover_simple_client [options]\n");
	printf("\toptions:\n");
	printf("\t\t-d, --dma-buf\n");
	printf("\t\t\tRun client with DMA-BUF\n");
	printf("\t\t-s, --shm\n");
	printf("\t\t\tRun client with share memory\n");
	printf("\t\t-n, --dev-node\n");
	printf("\t\t\tDRM device node. e.g. /dev/dri/renderD128 as default.\n");
	printf("\t\t-x, --left=x position\n");
	printf("\t\t-y, --top=y position\n");
	printf("\t\t-w, --width=window width\n");
	printf("\t\t-h, --height=window height\n");
	printf("\t\t-o, --output=primary output\n");
}

char drm_node[] = "/dev/dri/renderD128";
char client_short_options[] = "dsn:w:h:x:y:o:";
u32 win_width, win_height;
s32 win_x, win_y;
s32 is_dmabuf = 0;
u32 output_index = 0;

struct option client_options[] = {
	{"dma-buf", 0, NULL, 'd'},
	{"shm", 0, NULL, 's'},
	{"drm-node", 1, NULL, 'n'},
	{"width", 1, NULL, 'w'},
	{"height", 1, NULL, 'h'},
	{"left", 1, NULL, 'x'},
	{"top", 1, NULL, 'y'},
	{"output", 1, NULL, 'o'},
};

struct dma_buf {
	EGLImageKHR image;
	GLuint gl_texture;
	GLuint gl_fbo;
	struct gbm_bo *bo;
	u32 w, h;
	u32 stride;
	s32 fd;
	u64 id;
};

struct shm_buf {
	u32 w, h;
	u32 stride;
	struct clv_shm shm;
	u64 id;
};

struct dmabuf_window;
struct shm_window;

enum client_display_type {
	DISP_TYPE_DMABUF = 0,
	DISP_TYPE_SHM,
};

struct client_display {
	enum client_display_type type;
	struct clv_event_loop *loop;
	struct clv_event_source *sock_event;
	struct clv_event_source *repaint_event;
	struct clv_event_source *collect_event;

	s32 exit;

	struct dmabuf_window *dmabuf_window;
	struct shm_window *shm_window;

	s32 sock;
	
	struct {
		EGLDisplay display;
		EGLContext context;
		PFNEGLCREATEIMAGEKHRPROC create_image;
		PFNEGLDESTROYIMAGEKHRPROC destroy_image;
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	} egl;

	struct {
		s32 drm_fd;
		struct gbm_device *device;
	} gbm;
};

struct shm_window {
	struct client_display *disp;
	s32 x, y;
	u32 w, h;
	enum clv_pixel_fmt pixel_fmt;
	struct shm_buf buf[2];

	s32 flip_pending;
	s32 back_buf;

	u8 *ipc_rx_buf;
	u32 ipc_rx_buf_sz;

	struct clv_surface_info s;
	struct clv_view_info v;
	struct clv_bo_info b;
	struct clv_commit_info c;

	u8 *create_surface_tx_cmd_t;
	u8 *create_surface_tx_cmd;
	u32 create_surface_tx_len;

	u8 *create_view_tx_cmd_t;
	u8 *create_view_tx_cmd;
	u32 create_view_tx_len;

	u8 *create_bo_tx_cmd_t;
	u8 *create_bo_tx_cmd;
	u32 create_bo_tx_len;

	u8 *commit_tx_cmd_t;
	u8 *commit_tx_cmd;
	u32 commit_tx_len;

	u8 *terminate_tx_cmd_t;
	u8 *terminate_tx_cmd;
	u32 terminate_tx_len;

	u64 link_id;
};

struct dmabuf_window {
	struct client_display *disp;
	s32 x, y;
	u32 w, h;
	enum clv_pixel_fmt pixel_fmt;
	struct dma_buf buf[2];
	struct {
		GLuint program;
		GLuint pos;
		GLuint color;
		GLuint offset_uniform;
	} gl;
	s32 flip_pending;
	s32 back_buf;

	u8 *ipc_rx_buf;
	u32 ipc_rx_buf_sz;

	struct clv_surface_info s;
	struct clv_view_info v;
	struct clv_bo_info b;
	struct clv_commit_info c;

	u8 *create_surface_tx_cmd_t;
	u8 *create_surface_tx_cmd;
	u32 create_surface_tx_len;

	u8 *create_view_tx_cmd_t;
	u8 *create_view_tx_cmd;
	u32 create_view_tx_len;

	u8 *create_bo_tx_cmd_t;
	u8 *create_bo_tx_cmd;
	u32 create_bo_tx_len;

	u8 *commit_tx_cmd_t;
	u8 *commit_tx_cmd;
	u32 commit_tx_len;

	u8 *terminate_tx_cmd_t;
	u8 *terminate_tx_cmd;
	u32 terminate_tx_len;

	u64 link_id;
};

static void destroy_dmabuf_display(struct client_display *disp)
{
	if (disp->gbm.device)
		gbm_device_destroy(disp->gbm.device);

	if (disp->gbm.drm_fd > 0)
		close(disp->gbm.drm_fd);

	if (disp->egl.context != EGL_NO_CONTEXT)
		eglDestroyContext(disp->egl.display, disp->egl.context);

	if (disp->egl.display != EGL_NO_DISPLAY)
		eglTerminate(disp->egl.display);
}

static void destroy_display(struct client_display *disp)
{
	if (disp->type == DISP_TYPE_DMABUF)
		destroy_dmabuf_display(disp);

	if (disp->sock > 0) {
		close(disp->sock);
		disp->sock = 0;
	}

	if (disp->sock_event) {
		clv_event_source_remove(disp->sock_event);
		disp->sock_event = NULL;
	}

	if (disp->repaint_event) {
		clv_event_source_remove(disp->repaint_event);
		disp->repaint_event = NULL;
	}

	if (disp->loop) {
		clv_event_loop_destroy(disp->loop);
		disp->loop = NULL;
	}

	free(disp);
}

static s32 display_set_up_gbm(struct client_display *disp, const char *drm_node)
{
	disp->gbm.drm_fd = open(drm_node, O_RDWR);
	if (disp->gbm.drm_fd < 0) {
		printf("failed to open drm node %s %s", drm_node,
		       strerror(errno));
		return -errno;
	}

	disp->gbm.device = gbm_create_device(disp->gbm.drm_fd);
	if (disp->gbm.device == NULL) {
		printf("failed to create gbm device.\n");
		return -errno;
	}

	return 0;
}

static s32 match_config_to_visual(EGLDisplay egl_display, EGLint visual_id,
				  EGLConfig *configs, s32 count)
{
	s32 i;
	EGLint id;

	for (i = 0; i < count; i++) {
		if (!eglGetConfigAttrib(egl_display, configs[i],
					EGL_NATIVE_VISUAL_ID, &id)) {
			clv_warn("get VISUAL_ID failed.");
			continue;
		} else {
			clv_debug("get VISUAL_ID ok. %u %u", id, visual_id);
		}
		if (id == visual_id)
			return i;
	}

	return -1;
}

static s32 egl_choose_config(struct client_display *disp, const EGLint *attribs,
			     const EGLint *visual_ids, const s32 count_ids,
			     EGLConfig *config_matched, EGLint *vid)
{
	EGLint count_configs = 0;
	EGLint count_matched = 0;
	EGLConfig *configs;
	s32 i, config_index = -1;

	if (!eglGetConfigs(disp->egl.display, NULL, 0, &count_configs)) {
		clv_err("Cannot get EGL configs.");
		return -1;
	}
	clv_notice("count_configs = %d", count_configs);

	configs = calloc(count_configs, sizeof(*configs));
	if (!configs)
		return -ENOMEM;

	if (!eglChooseConfig(disp->egl.display, attribs, configs,
			     count_configs, &count_matched)
	    || !count_matched) {
		clv_err("cannot select appropriate configs.");
		goto out1;
	}
	clv_info("count_matched = %d", count_matched);

	if (!visual_ids || count_ids == 0)
		config_index = 0;

	for (i = 0; config_index == -1 && i < count_ids; i++) {
		config_index = match_config_to_visual(disp->egl.display,
						      visual_ids[i],
						      configs,
						      count_matched);
		clv_info("config_index = %d i = %d count_ids = %d",
			  config_index, i, count_ids);
	}

	if (config_index != -1)
		*config_matched = configs[config_index];

out1:
	if (visual_ids) {
		*vid = visual_ids[i - 1];
	} else {
		for (i = 0; i < count_matched; i++) {
			if (!eglGetConfigAttrib(disp->egl.display, configs[0],
						EGL_NATIVE_VISUAL_ID, vid)) {
				clv_err("Get visual id failed.");
				continue;
			}
			break;
		}
	}
	free(configs);
	if (config_index == -1)
		return -1;

	if (i > 1)
		clv_warn("Unable to use first choice EGL config with ID "
			 "0x%x, succeeded with alternate ID 0x%x",
			 visual_ids[0], visual_ids[i - 1]);

	return 0;
}

static s32 display_set_up_egl(struct client_display *disp)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	static const EGLint opaque_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};
	EGLint major, minor;
	static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;
	EGLConfig egl_config;
	s32 format = GBM_FORMAT_ARGB8888;
	s32 vid;

	if (!get_platform_display) {
		get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        		eglGetProcAddress("eglGetPlatformDisplayEXT");
	}
	disp->egl.display = get_platform_display(EGL_PLATFORM_GBM_KHR,
						 disp->gbm.device, NULL);
	if (disp->egl.display == EGL_NO_DISPLAY) {
		clv_err("Failed to create EGLDisplay");
		goto error;
	}

	if (eglInitialize(disp->egl.display, &major, &minor) == EGL_FALSE) {
		clv_err("Failed to initialize EGLDisplay");
		goto error;
	}

	if (egl_choose_config(disp, opaque_attribs, &format, 1,
			      &egl_config, &vid) < 0) {
		clv_err("failed to choose config");
		goto error;
	}
	printf("vid = %08X\n", vid);

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		clv_err("Failed to bind OpenGL ES API");
		goto error;
	}

	disp->egl.context = eglCreateContext(disp->egl.display,
						egl_config,
						EGL_NO_CONTEXT,
						context_attribs);
	if (disp->egl.context == EGL_NO_CONTEXT) {
		fprintf(stderr, "Failed to create EGLContext\n");
		goto error;
	}

	eglMakeCurrent(disp->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       disp->egl.context);

	disp->egl.create_image =
		(void *)eglGetProcAddress("eglCreateImageKHR");
	assert(disp->egl.create_image);

	disp->egl.destroy_image =
		(void *)eglGetProcAddress("eglDestroyImageKHR");
	assert(disp->egl.destroy_image);

	disp->egl.image_target_texture_2d =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	assert(disp->egl.image_target_texture_2d);

	return 0;

error:
	return -1;
}

static struct client_display *create_dmabuf_display(const char *node)
{
	struct client_display *disp;

	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return NULL;

	disp->type = DISP_TYPE_DMABUF;
	disp->gbm.drm_fd = -1;
	disp->sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	assert(disp->sock > 0);
	//clv_socket_nonblock(disp->sock);
	assert(clv_socket_connect(disp->sock, "/tmp/CLV_SERVER") == 0);
	if (display_set_up_gbm(disp, node) < 0)
		goto error;

	if (display_set_up_egl(disp) < 0)
		goto error;

	printf("create client display ok.\n");
	return disp;

error:
	if (disp)
		destroy_display(disp);
	return NULL;
}

static struct client_display *create_shm_display(void)
{
	struct client_display *disp;

	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return NULL;

	disp->type = DISP_TYPE_SHM;
	disp->sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	assert(disp->sock > 0);
	//clv_socket_nonblock(disp->sock);
	assert(clv_socket_connect(disp->sock, "/tmp/CLV_SERVER") == 0);
	printf("create client display ok.\n");
	return disp;
}

static struct client_display *create_client_display(const char *hwaccel_node)
{
	if (hwaccel_node)
		return create_dmabuf_display(hwaccel_node);
	else
		return create_shm_display();
}

static const char *egl_strerror(EGLint err)
{
#define EGLERROR(x) case x: return #x;
	switch (err) {
	EGLERROR(EGL_SUCCESS)
	EGLERROR(EGL_NOT_INITIALIZED)
	EGLERROR(EGL_BAD_ACCESS)
	EGLERROR(EGL_BAD_ALLOC)
	EGLERROR(EGL_BAD_ATTRIBUTE)
	EGLERROR(EGL_BAD_CONTEXT)
	EGLERROR(EGL_BAD_CONFIG)
	EGLERROR(EGL_BAD_CURRENT_SURFACE)
	EGLERROR(EGL_BAD_DISPLAY)
	EGLERROR(EGL_BAD_SURFACE)
	EGLERROR(EGL_BAD_MATCH)
	EGLERROR(EGL_BAD_PARAMETER)
	EGLERROR(EGL_BAD_NATIVE_PIXMAP)
	EGLERROR(EGL_BAD_NATIVE_WINDOW)
	EGLERROR(EGL_CONTEXT_LOST)
	default:
		return "unknown";
	}
#undef EGLERROR
}

static void egl_error_state(void)
{
	EGLint err;

	err = eglGetError();
	clv_err("EGL err: %s (0x%04lX)", egl_strerror(err), (u64)err);
}

static s32 create_fbo_for_buffer(struct client_display *display,
				 struct dma_buf *buffer)
{
	static const int general_attribs = 3;
	static const int plane_attribs = 5;
	static const int entries_per_attrib = 2;
	EGLint attribs[(general_attribs + plane_attribs * 4) *
			entries_per_attrib + 1];
	unsigned int atti = 0;

	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = buffer->w;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = buffer->h;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = DRM_FORMAT_ARGB8888;

#define ADD_PLANE_ATTRIBS(plane_idx) { \
	attribs[atti++] = EGL_DMA_BUF_PLANE ## plane_idx ## _FD_EXT; \
	attribs[atti++] = buffer->fd; \
	attribs[atti++] = EGL_DMA_BUF_PLANE ## plane_idx ## _OFFSET_EXT; \
	attribs[atti++] = 0; \
	attribs[atti++] = EGL_DMA_BUF_PLANE ## plane_idx ## _PITCH_EXT; \
	attribs[atti++] = buffer->stride; \
	printf("attribs[%d] = %u\n", atti-1, attribs[atti-1]); \
	}

	ADD_PLANE_ATTRIBS(0);

#undef ADD_PLANE_ATTRIBS

	attribs[atti] = EGL_NONE;

	assert(atti < ARRAY_SIZE(attribs));

	buffer->image = display->egl.create_image(display->egl.display,
						  EGL_NO_CONTEXT,
						  EGL_LINUX_DMA_BUF_EXT,
						  NULL, attribs);
	if (buffer->image == EGL_NO_IMAGE_KHR) {
		egl_error_state();
	}
	assert(buffer->image != EGL_NO_IMAGE_KHR);

	eglMakeCurrent(display->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			display->egl.context);

	glGenTextures(1, &buffer->gl_texture);
	glBindTexture(GL_TEXTURE_2D, buffer->gl_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	display->egl.image_target_texture_2d(GL_TEXTURE_2D, buffer->image);

	glGenFramebuffers(1, &buffer->gl_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->gl_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, buffer->gl_texture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "FBO creation failed\n");
		return -1;
	}

	return 0;
}

static s32 create_dmabuf(struct client_display *disp, struct dma_buf *buffer)
{
	buffer->bo = gbm_bo_create(disp->gbm.device,
				   buffer->w,
				   buffer->h,
				   GBM_FORMAT_ARGB8888,
				   GBM_BO_USE_RENDERING);
	assert(buffer->bo);

	buffer->stride = gbm_bo_get_stride(buffer->bo);
	printf("buffer->stride = %d\n", buffer->stride);
	buffer->fd = gbm_bo_get_fd(buffer->bo);
	assert(buffer->fd);

	assert(create_fbo_for_buffer(disp, buffer) == 0);

	return 0;
}

static void create_shmbuf(struct client_display *disp, struct shm_buf *buffer,
			  u32 index)
{
	char name[CLV_BUFFER_NAME_LEN];

	memset(name, 0, CLV_BUFFER_NAME_LEN);
	sprintf(name, "simple_client-%d-%u", getpid(), index);
	clv_shm_init(&buffer->shm, name, buffer->stride * buffer->h, 1);
}

static const char *vert_shader_text =
	"uniform float offset;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_Position = pos + vec4(offset, offset, 0.0, 0.0);\n"
	"  v_color = color;\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_FragColor = v_color;\n"
	"}\n";

static GLuint create_shader(const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	assert(shader != 0);

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		return 0;
	}

	return shader;
}

static GLuint create_and_link_program(GLuint vert, GLuint frag)
{
	GLint status;
	GLuint program = glCreateProgram();

	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		return 0;
	}

	return program;
}
static s32 window_set_up_gl(struct dmabuf_window *window)
{
	GLuint vert = create_shader(
		vert_shader_text,
		GL_VERTEX_SHADER);
	GLuint frag = create_shader(
		frag_shader_text,
		GL_FRAGMENT_SHADER);

	window->gl.program = create_and_link_program(vert, frag);

	glDeleteShader(vert);
	glDeleteShader(frag);

	window->gl.pos = glGetAttribLocation(window->gl.program, "pos");
	window->gl.color = glGetAttribLocation(window->gl.program, "color");

	glUseProgram(window->gl.program);

	window->gl.offset_uniform =
		glGetUniformLocation(window->gl.program, "offset");

	return window->gl.program == 0;
}

static struct dmabuf_window *create_dmabuf_window(struct client_display *disp,
						  s32 x, s32 y, u32 w, u32 h)
{
	s32 i, ret;
	struct dmabuf_window *win;
	u32 n;

	win = calloc(1, sizeof(*win));
	if (!win)
		return NULL;

	win->disp = disp;

	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	for (i = 0; i < 2; i++) {
		win->buf[i].w = win->w;
		win->buf[i].h = win->h;
		ret = create_dmabuf(disp, &win->buf[i]);
		assert(ret == 0);
	}

	win->flip_pending = 0;
	win->back_buf = 0;

	assert(window_set_up_gl(win) == 0);

	win->ipc_rx_buf_sz = 32 * 1024;
	win->ipc_rx_buf = malloc(win->ipc_rx_buf_sz);
	if (!win->ipc_rx_buf)
		return NULL;

	win->s.is_opaque = 1;
	win->s.damage.pos.x = 0;
	win->s.damage.pos.y = 0;
	win->s.damage.w = win->w;
	win->s.damage.h = win->h;
	win->s.opaque.pos.x = 0;
	win->s.opaque.pos.y = 0;
	win->s.opaque.w = win->w;
	win->s.opaque.h = win->h;
	win->s.width = win->w;
	win->s.height = win->h;
	win->v.type = CLV_VIEW_TYPE_PRIMARY;
	win->v.area.pos.x = win->x;
	win->v.area.pos.y = win->y;
	win->v.area.w = win->w;
	win->v.area.h = win->h;
	win->v.alpha = 1.0f;
	win->v.primary_output = output_index;
	//win->v.output_mask = 0x03;
	win->v.output_mask = 1 << output_index;
	win->b.type = CLV_BUF_TYPE_DMA;
	win->b.fmt = CLV_PIXEL_FMT_ARGB8888;
	win->b.internal_fmt = DRM_FORMAT_ARGB8888;
	win->b.width = win->w;
	//TODO win->b.stride = win->w * 4;
	win->b.height = win->h;
	win->c.shown = 1;
	win->c.view_x = win->x;
	win->c.view_y = win->y;
	win->c.view_width = win->w;
	win->c.view_height = win->h;
	win->c.delta_z = 0;

	win->create_surface_tx_cmd_t = clv_client_create_surface_cmd(&win->s,
								     &n);
	assert(win->create_surface_tx_cmd_t);
	win->create_surface_tx_cmd = malloc(n);
	assert(win->create_surface_tx_cmd);
	win->create_surface_tx_len = n;

	win->create_view_tx_cmd_t = clv_client_create_view_cmd(&win->v, &n);
	assert(win->create_view_tx_cmd_t);
	win->create_view_tx_cmd = malloc(n);
	assert(win->create_view_tx_cmd);
	win->create_view_tx_len = n;

	win->create_bo_tx_cmd_t = clv_client_create_bo_cmd(&win->b, &n);
	assert(win->create_bo_tx_cmd_t);
	win->create_bo_tx_cmd = malloc(n);
	assert(win->create_bo_tx_cmd);
	win->create_bo_tx_len = n;

	win->commit_tx_cmd_t = clv_client_create_commit_req_cmd(&win->c, &n);
	assert(win->commit_tx_cmd_t);
	win->commit_tx_cmd = malloc(n);
	assert(win->commit_tx_cmd);
	win->commit_tx_len = n;

	win->terminate_tx_cmd_t = clv_client_create_destroy_cmd(0, &n);
	assert(win->terminate_tx_cmd_t);
	win->terminate_tx_cmd = malloc(n);
	assert(win->terminate_tx_cmd);
	win->terminate_tx_len = n;

	return win;
}

static struct shm_window *create_shmbuf_window(struct client_display *disp,
					       s32 x, s32 y, u32 w, u32 h)
{
	struct shm_window *win;
	u32 n, i;

	win = calloc(1, sizeof(*win));
	if (!win)
		return NULL;

	win->disp = disp;

	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	for (i = 0; i < 2; i++) {
		win->buf[i].w = win->w;
		win->buf[i].stride = win->w * 4;
		win->buf[i].h = win->h;
		create_shmbuf(disp, &win->buf[i], i);
	}

	win->flip_pending = 0;
	win->back_buf = 0;

	win->ipc_rx_buf_sz = 32 * 1024;
	win->ipc_rx_buf = malloc(win->ipc_rx_buf_sz);
	if (!win->ipc_rx_buf)
		return NULL;

	win->s.is_opaque = 1;
	win->s.damage.pos.x = 0;
	win->s.damage.pos.y = 0;
	win->s.damage.w = win->w;
	win->s.damage.h = win->h;
	win->s.opaque.pos.x = 0;
	win->s.opaque.pos.y = 0;
	win->s.opaque.w = win->w;
	win->s.opaque.h = win->h;
	win->s.width = win->w;
	win->s.height = win->h;
	win->v.type = CLV_VIEW_TYPE_PRIMARY;
	win->v.area.pos.x = win->x;
	win->v.area.pos.y = win->y;
	win->v.area.w = win->w;
	win->v.area.h = win->h;
	win->v.alpha = 1.0f;
	//win->v.output_mask = 0x03;
	win->v.output_mask = 1 << output_index;
	win->v.primary_output = output_index;
	win->b.type = CLV_BUF_TYPE_SHM;
	win->b.fmt = CLV_PIXEL_FMT_ARGB8888;
	win->b.count_planes = 1;
	win->b.internal_fmt = 0;
	win->b.width = win->w;
	win->b.stride = win->w * 4;
	win->b.height = win->h;
	win->c.shown = 1;
	win->c.view_x = win->x;
	win->c.view_y = win->y;
	win->c.view_width = win->w;
	win->c.view_height = win->h;
	win->c.delta_z = 0;

	win->create_surface_tx_cmd_t = clv_client_create_surface_cmd(&win->s,
								     &n);
	assert(win->create_surface_tx_cmd_t);
	win->create_surface_tx_cmd = malloc(n);
	assert(win->create_surface_tx_cmd);
	win->create_surface_tx_len = n;

	win->create_view_tx_cmd_t = clv_client_create_view_cmd(&win->v, &n);
	assert(win->create_view_tx_cmd_t);
	win->create_view_tx_cmd = malloc(n);
	assert(win->create_view_tx_cmd);
	win->create_view_tx_len = n;

	win->create_bo_tx_cmd_t = clv_client_create_bo_cmd(&win->b, &n);
	assert(win->create_bo_tx_cmd_t);
	win->create_bo_tx_cmd = malloc(n);
	assert(win->create_bo_tx_cmd);
	win->create_bo_tx_len = n;

	win->commit_tx_cmd_t = clv_client_create_commit_req_cmd(&win->c, &n);
	assert(win->commit_tx_cmd_t);
	win->commit_tx_cmd = malloc(n);
	assert(win->commit_tx_cmd);
	win->commit_tx_len = n;

	win->terminate_tx_cmd_t = clv_client_create_destroy_cmd(0, &n);
	assert(win->terminate_tx_cmd_t);
	win->terminate_tx_cmd = malloc(n);
	assert(win->terminate_tx_cmd);
	win->terminate_tx_len = n;

	return win;
}

static void run_client_event_loop(struct client_display *disp)
{
	while (!disp->exit) {
		clv_event_loop_dispatch(disp->loop, -1);
	}
}

/* Renders a square moving from the lower left corner to the
 * upper right corner of the window. The square's vertices have
 * the following colors:
 *
 *  green +-----+ yellow
 *        |     |
 *        |     |
 *    red +-----+ blue
 */
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

static inline s64 timespec_to_nsec(const struct timespec *a)
{
	return (s64)a->tv_sec * NSEC_PER_SEC + a->tv_nsec;
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

static void render_gpu(struct dmabuf_window *window, struct dma_buf *buffer)
{
	/* Complete a movement iteration in 5000 ms. */
	static const u64 iteration_ms = 15000;
	static const GLfloat verts[4][2] = {
		{ -0.5, -0.5 },
		{ -0.5,  0.5 },
		{  0.5, -0.5 },
		{  0.5,  0.5 }
	};
	static const GLfloat colors[4][3] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 },
		{ 1, 1, 0 }
	};
	GLfloat offset;
	struct timeval tv;
	struct timespec t1, t2, t3;
	u64 time_ms;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	gettimeofday(&tv, NULL);
	time_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	/* Split time_ms in repeating windows of [0, iteration_ms) and map them
	 * to offsets in the [-0.5, 0.5) range. */
	offset = (time_ms % iteration_ms) / (float) iteration_ms - 0.5;

	/* Direct all GL draws to the buffer through the FBO */
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->gl_fbo);

	glViewport(0, 0, window->w, window->h);

	glUniform1f(window->gl.offset_uniform, offset);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(window->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(window->gl.color, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.color);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.color);

	clock_gettime(CLOCK_MONOTONIC, &t2);
	glFinish();
	clock_gettime(CLOCK_MONOTONIC, &t3);
	//printf("client render spent %ld ms\n",
	//	timespec_sub_to_msec(&t2, &t1));
	//printf("client gl flush spent %ld ms\n",
	//	timespec_sub_to_msec(&t3, &t2));
}

static void dmabuf_redraw(void *data)
{
	struct dmabuf_window *window = data;
	struct client_display *disp = window->disp;
	struct dma_buf *buffer;

	assert(clv_event_source_timer_update(disp->repaint_event, 8, 0) == 0);
	buffer = &window->buf[window->back_buf];

	render_gpu(window, buffer);
	if (window->flip_pending) {
		assert(clv_event_source_timer_update(disp->repaint_event, 2, 667) ==0);
//		clv_debug("not commit");
		return;
	}

	window->c.bo_id = buffer->id;
	clv_dup_commit_req_cmd(window->commit_tx_cmd, window->commit_tx_cmd_t,
			       window->commit_tx_len, &window->c);
	//clv_debug("commit %lu", buffer->id);
	window->flip_pending = 1;
	window->back_buf = 1 - window->back_buf;
	clv_send(disp->sock, window->commit_tx_cmd, window->commit_tx_len);
}

static void render_cpu(struct shm_window *window, struct shm_buf *buffer)
{
	u32 i, *pixel;
	static u32 c = 0x80FF00;

#if 1
	pixel = (u32 *)(buffer->shm.map);
//	printf("render_cpu %p >>>>>>>>>>>> 0x%08X\n", pixel,
//		0xFF000000 | c);
	for (i = 0; i < buffer->shm.sz / 4; i++) {
		pixel[i] = (0xFF000000 | c);
	}
	c -= 0x001000;
	if (c <= 0x000000)
		c = 0x80F000;
#else
	static s32 first = 2;

	pixel = (u32 *)(buffer->shm.map);
	if (first) {
		first--;
		for (i = 0; i < buffer->shm.sz / 4; i++) {
			pixel[i] = (0xFF00FF00);
		}
	}
#endif
}

static void shmbuf_redraw(void *data)
{
	struct shm_window *window = data;
	struct client_display *disp = window->disp;
	struct shm_buf *buffer;

	assert(clv_event_source_timer_update(disp->repaint_event, 8, 0) == 0);
	buffer = &window->buf[window->back_buf];

	if (window->flip_pending) {
		assert(clv_event_source_timer_update(disp->repaint_event, 2, 667) ==0);
//		clv_debug("not commit");
		return;
	}
	render_cpu(window, buffer);

	window->c.bo_id = buffer->id;
#if 1
	window->c.bo_damage.pos.x = buffer->w / 3;
	window->c.bo_damage.pos.y = buffer->h / 3;
	window->c.bo_damage.w = buffer->w / 3;
	window->c.bo_damage.h = buffer->h / 3;
#else
	window->c.bo_damage.pos.x = 0;
	window->c.bo_damage.pos.y = 0;
	window->c.bo_damage.w = buffer->w;
	window->c.bo_damage.h = buffer->h;
#endif

	clv_dup_commit_req_cmd(window->commit_tx_cmd, window->commit_tx_cmd_t,
			       window->commit_tx_len, &window->c);
	//clv_debug("commit %lu", buffer->id);
	window->flip_pending = 1;
	window->back_buf = 1 - window->back_buf;
	clv_send(disp->sock, window->commit_tx_cmd, window->commit_tx_len);
}

static s32 dmabuf_repaint_cb(void *data)
{
	struct client_display *disp = data;
	struct dmabuf_window *window = disp->dmabuf_window;

	dmabuf_redraw(window);
	return 0;
}

static s32 shm_repaint_cb(void *data)
{
	struct client_display *disp = data;
	struct shm_window *window = disp->shm_window;

	shmbuf_redraw(window);
	return 0;
}

static s32 collect_cb(void *data)
{
	struct client_display *disp = data;

	printf("frame_cnt = %u\n", frame_cnt);
	frame_cnt = 0;
	assert(clv_event_source_timer_update(disp->collect_event, 1000, 0)== 0);
	return 0;
}

static s32 dmabuf_client_event_cb(s32 fd, u32 mask, void *data)
{
	s32 ret;
	struct client_display *display = data;
	struct dmabuf_window *window = display->dmabuf_window;
	struct clv_tlv *tlv;
	u8 *rx_p;
	u32 flag, length;
	u64 id;
	static s32 bo_0_created = 0;

	ret = clv_recv(fd, window->ipc_rx_buf, sizeof(*tlv) + sizeof(u32));
	if (ret == -1) {
		clv_err("server exit.");
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive server cmd");
	}
	tlv = (struct clv_tlv *)(window->ipc_rx_buf + sizeof(u32));
	length = tlv->length;
	rx_p = window->ipc_rx_buf + sizeof(u32) + sizeof(*tlv);
	flag = *((u32 *)(window->ipc_rx_buf));
	ret = clv_recv(fd, rx_p, length);
	if (ret == -1) {
		clv_err("server exit.");
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive server cmd");
		return -1;
	}

	if (tlv->tag == CLV_TAG_INPUT) {
		if (flag & CLV_CMD_INPUT_EVT_SHIFT) {

		} else {
			clv_err("unknown command 0x%08X, not a input cmd.",
				flag);
			return -1;
		}
	} else if (tlv->tag == CLV_TAG_WIN) {
		if (flag & (1 << CLV_CMD_LINK_ID_ACK_SHIFT)) {
			id = clv_client_parse_link_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create link failed.");
				return -1;
			}
			window->link_id = id;
			clv_debug("link_id: 0x%08lX", window->link_id);
			(void)clv_dup_create_surface_cmd(
					window->create_surface_tx_cmd,
					window->create_surface_tx_cmd_t,
					window->create_surface_tx_len,
					&window->s);
			ret = clv_send(fd, window->create_surface_tx_cmd,
				       window->create_surface_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create surface cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_SURFACE_ACK_SHIFT)) {
			id = clv_client_parse_surface_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create surface failed.");
				return -1;
			}
			window->s.surface_id = id;
			clv_debug("create surface ok, surface_id = 0x%08lX",
				  window->s.surface_id);
			(void)clv_dup_create_view_cmd(
					window->create_view_tx_cmd,
					window->create_view_tx_cmd_t,
					window->create_view_tx_len,
					&window->v);
			ret = clv_send(fd, window->create_view_tx_cmd,
				       window->create_view_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create view cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_VIEW_ACK_SHIFT)) {
			id = clv_client_parse_view_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create view failed.");
				return -1;
			}
			window->v.view_id = id;
			clv_debug("create view ok, view_id: 0x%08lX",
				  window->v.view_id);
			window->b.stride = window->buf[0].stride;
			window->b.surface_id = window->s.surface_id;
			(void)clv_dup_create_bo_cmd(
					window->create_bo_tx_cmd,
					window->create_bo_tx_cmd_t,
					window->create_bo_tx_len,
					&window->b);
			ret = clv_send(fd, window->create_bo_tx_cmd,
				       window->create_bo_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create bo cmd");
				return -1;
			}
			ret = clv_send_fd(fd, window->buf[0].fd);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send send bo[0]'s fd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_BO_ACK_SHIFT)) {
			id = clv_client_parse_bo_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create bo failed.");
				return -1;
			}
			if (!bo_0_created) {
				window->buf[0].id = id;
				clv_debug("create bo[0] ok, bo_id: 0x%08lX",
					  window->buf[0].id);
				bo_0_created = 1;
				window->b.stride = window->buf[1].stride;
				window->b.surface_id = window->s.surface_id;
				(void)clv_dup_create_bo_cmd(
						window->create_bo_tx_cmd,
						window->create_bo_tx_cmd_t,
						window->create_bo_tx_len,
						&window->b);
				ret = clv_send(fd, window->create_bo_tx_cmd,
					       window->create_bo_tx_len);
				if (ret == -1) {
					clv_err("server exit.");
					return -1;
				} else if (ret < 0) {
					clv_err("failed to send create bo cmd");
					return -1;
				}
				ret = clv_send_fd(fd, window->buf[1].fd);
				if (ret == -1) {
					clv_err("server exit.");
					return -1;
				} else if (ret < 0) {
					clv_err("failed to send send bo[1]'s "
						"fd");
					return -1;
				}
			} else {
				window->buf[1].id = id;
				clv_debug("create bo[1] ok, bo_id: 0x%08lX",
					  window->buf[1].id);
				clv_debug("Start to draw.");
				if (window->b.type == CLV_BUF_TYPE_DMA)
					dmabuf_redraw(window);
				else
					shmbuf_redraw(window);
				clv_event_source_timer_update(
					display->collect_event, 1000, 0);
			}
		} else if (flag & (1 << CLV_CMD_COMMIT_ACK_SHIFT)) {
			clv_client_parse_commit_ack_cmd(window->ipc_rx_buf);
			//clv_debug("receive commit ack");
		} else if (flag & (1 << CLV_CMD_BO_COMPLETE_SHIFT)) {
			id=clv_client_parse_bo_complete_cmd(window->ipc_rx_buf);
			//clv_debug("receive bo complete %lu", id);
			frame_cnt++;
			window->flip_pending = 0;
		} else if (flag & (1 << CLV_CMD_SHELL_SHIFT)) {
			clv_debug("receive shell event");
		} else if (flag & (1 << CLV_CMD_DESTROY_ACK_SHIFT)) {
			clv_debug("receive destroy ack");
		} else {
			clv_err("unknown command 0x%08X", flag);
			return -1;
		}
	} else {
		clv_err("command is not a win/input command");
		return -1;
	}

	return 0;
}

static s32 shm_client_event_cb(s32 fd, u32 mask, void *data)
{
	s32 ret;
	struct client_display *display = data;
	struct shm_window *window = display->shm_window;
	struct clv_tlv *tlv;
	u8 *rx_p;
	u32 flag, length;
	u64 id;
	static s32 bo_0_created = 0;

	ret = clv_recv(fd, window->ipc_rx_buf, sizeof(*tlv) + sizeof(u32));
	if (ret == -1) {
		clv_err("server exit.");
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive server cmd");
	}
	tlv = (struct clv_tlv *)(window->ipc_rx_buf + sizeof(u32));
	length = tlv->length;
	rx_p = window->ipc_rx_buf + sizeof(u32) + sizeof(*tlv);
	flag = *((u32 *)(window->ipc_rx_buf));
	ret = clv_recv(fd, rx_p, length);
	if (ret == -1) {
		clv_err("server exit.");
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive server cmd");
		return -1;
	}

	if (tlv->tag == CLV_TAG_INPUT) {
		if (flag & CLV_CMD_INPUT_EVT_SHIFT) {

		} else {
			clv_err("unknown command 0x%08X, not a input cmd.",
				flag);
			return -1;
		}
	} else if (tlv->tag == CLV_TAG_WIN) {
		if (flag & (1 << CLV_CMD_LINK_ID_ACK_SHIFT)) {
			id = clv_client_parse_link_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create link failed.");
				return -1;
			}
			window->link_id = id;
			clv_debug("link_id: 0x%08lX", window->link_id);
			(void)clv_dup_create_surface_cmd(
					window->create_surface_tx_cmd,
					window->create_surface_tx_cmd_t,
					window->create_surface_tx_len,
					&window->s);
			ret = clv_send(fd, window->create_surface_tx_cmd,
				       window->create_surface_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create surface cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_SURFACE_ACK_SHIFT)) {
			id = clv_client_parse_surface_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create surface failed.");
				return -1;
			}
			window->s.surface_id = id;
			clv_debug("create surface ok, surface_id = 0x%08lX",
				  window->s.surface_id);
			(void)clv_dup_create_view_cmd(
					window->create_view_tx_cmd,
					window->create_view_tx_cmd_t,
					window->create_view_tx_len,
					&window->v);
			ret = clv_send(fd, window->create_view_tx_cmd,
				       window->create_view_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create view cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_VIEW_ACK_SHIFT)) {
			id = clv_client_parse_view_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create view failed.");
				return -1;
			}
			window->v.view_id = id;
			clv_debug("create view ok, view_id: 0x%08lX",
				  window->v.view_id);
			window->b.stride = window->buf[0].stride;
			strcpy(window->b.name, window->buf[0].shm.name);
			window->b.surface_id = window->s.surface_id;
			(void)clv_dup_create_bo_cmd(
					window->create_bo_tx_cmd,
					window->create_bo_tx_cmd_t,
					window->create_bo_tx_len,
					&window->b);
			ret = clv_send(fd, window->create_bo_tx_cmd,
				       window->create_bo_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create bo cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_BO_ACK_SHIFT)) {
			id = clv_client_parse_bo_id(window->ipc_rx_buf);
			if (id == 0) {
				clv_err("create bo failed.");
				return -1;
			}
			if (!bo_0_created) {
				window->buf[0].id = id;
				clv_debug("create bo[0] ok, bo_id: 0x%08lX",
					  window->buf[0].id);
				bo_0_created = 1;
				window->b.stride = window->buf[1].stride;
				strcpy(window->b.name, window->buf[1].shm.name);
				window->b.surface_id = window->s.surface_id;
				(void)clv_dup_create_bo_cmd(
						window->create_bo_tx_cmd,
						window->create_bo_tx_cmd_t,
						window->create_bo_tx_len,
						&window->b);
				ret = clv_send(fd, window->create_bo_tx_cmd,
					       window->create_bo_tx_len);
				if (ret == -1) {
					clv_err("server exit.");
					return -1;
				} else if (ret < 0) {
					clv_err("failed to send create bo cmd");
					return -1;
				}
			} else {
				window->buf[1].id = id;
				clv_debug("create bo[1] ok, bo_id: 0x%08lX",
					  window->buf[1].id);
				clv_debug("Start to draw.");
				shmbuf_redraw(window);
				clv_event_source_timer_update(
					display->collect_event, 1000, 0);
			}
		} else if (flag & (1 << CLV_CMD_COMMIT_ACK_SHIFT)) {
			clv_client_parse_commit_ack_cmd(window->ipc_rx_buf);
			//clv_debug("receive commit ack");
		} else if (flag & (1 << CLV_CMD_BO_COMPLETE_SHIFT)) {
			id=clv_client_parse_bo_complete_cmd(window->ipc_rx_buf);
			//clv_debug("receive bo complete %lu", id);
			frame_cnt++;
			window->flip_pending = 0;
		} else if (flag & (1 << CLV_CMD_SHELL_SHIFT)) {
			clv_debug("receive shell event");
		} else if (flag & (1 << CLV_CMD_DESTROY_ACK_SHIFT)) {
			clv_debug("receive destroy ack");
		} else {
			clv_err("unknown command 0x%08X", flag);
			return -1;
		}
	} else {
		clv_err("command is not a win/input command");
		return -1;
	}

	return 0;
}

void run_shm_client(void)
{
	struct client_display *display;
	struct shm_window *win;

	display = create_client_display(drm_node);
	if (!display) {
		clv_err("create client_display failed.");
		return;
	}

	win = create_shmbuf_window(display, win_x, win_y, win_width,win_height);
	if (!win) {
		clv_err("create client display failed.");
		return;
	}

	display->shm_window = win;
	win->disp = display;
	display->loop = clv_event_loop_create();
	assert(display->loop);
	display->sock_event = clv_event_loop_add_fd(display->loop,
						    display->sock,
						    CLV_EVT_READABLE,
						    shm_client_event_cb,
						    display);
	assert(display->sock_event);
	display->repaint_event = clv_event_loop_add_timer(display->loop,
							  shm_repaint_cb,
							  display);
	assert(display->repaint_event);

	display->collect_event = clv_event_loop_add_timer(display->loop,
							  collect_cb,
							  display);

	run_client_event_loop(display);
}

void run_dmabuf_client(void)
{
	struct client_display *display;
	struct dmabuf_window *win;

	display = create_client_display(drm_node);
	if (!display) {
		clv_err("create client_display failed.");
		return;
	}

	win = create_dmabuf_window(display, win_x, win_y, win_width,win_height);
	if (!win) {
		clv_err("create client display failed.");
		return;
	}

	display->dmabuf_window = win;
	win->disp = display;
	display->loop = clv_event_loop_create();
	assert(display->loop);
	display->sock_event = clv_event_loop_add_fd(display->loop,
						    display->sock,
						    CLV_EVT_READABLE,
						    dmabuf_client_event_cb,
						    display);
	assert(display->sock_event);
	display->repaint_event = clv_event_loop_add_timer(display->loop,
							  dmabuf_repaint_cb,
							  display);
	assert(display->repaint_event);

	display->collect_event = clv_event_loop_add_timer(display->loop,
							  collect_cb,
							  display);

	run_client_event_loop(display);
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(4, sizeof(set), &set);

	while ((ch = getopt_long(argc, argv, client_short_options,
				 client_options, NULL)) != -1) {
		switch (ch) {
		case 'n':
			strcpy(drm_node, optarg);
			break;
		case 'd':
			is_dmabuf = 1;
			break;
		case 's':
			is_dmabuf = 0;
			break;
		case 'w':
			win_width = atoi(optarg);
			break;
		case 'h':
			win_height = atoi(optarg);
			break;
		case 'x':
			win_x = atoi(optarg);
			break;
		case 'y':
			win_y = atoi(optarg);
			break;
		case 'o':
			output_index = atoi(optarg);
			break;
		default:
			usage();
			return -1;
		}
	}

	if (is_dmabuf) {
		clv_info("Client buffer type: DMA-BUF");
	} else {
		clv_info("Client buffer type: SHM-BUF");
	}
	clv_info("Render Device: %s", drm_node);
	clv_info("Window rect: %d,%d %ux%u", win_x, win_y, win_width,
		 win_height);

	if (is_dmabuf) {
		run_dmabuf_client();
	} else {
		run_shm_client();
	}

	return 0;
}

