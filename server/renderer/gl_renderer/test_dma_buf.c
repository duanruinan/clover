#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <clover_compositor.h>
#include <clover_utils.h>
#include <clover_region.h>
#include <clover_ipc.h>
#include <clover_log.h>
#include <clover_event.h>

static void usage(void)
{
	printf("./test_dma_buf server output_w output_h\n");
	printf("./test_dma_buf client left top client_w client_h\n");
}

struct dma_buf {
	EGLImageKHR image;
	GLuint gl_texture;
	GLuint gl_fbo;
	struct gbm_bo *bo;
	u32 w, h;
	u32 stride;
	s32 fd;
	void *buf_handle;
};

struct ipc_cmd {
	void *link;
	void *surface;
	void *view;
	void *buf;
	u32 status;
	s32 x, y;
	u32 w, h;
	u32 stride;
	s32 fd;
	void *buf_handle;
};

struct client_display;
struct client_window {
	s32 x, y;
	u32 w, h;
	enum clv_pixel_fmt pixel_fmt;
	struct dma_buf buf;
	struct {
		GLuint program;
		GLuint pos;
		GLuint color;
		GLuint offset_uniform;
	} gl;
	struct ipc_cmd cmd;
	struct client_display *disp;
	s32 flip_occur;
};

struct client_display {
	struct clv_event_loop *loop;
	struct clv_event_source *sock_event;
	struct clv_event_source *repaint_event;

	s32 exit;

	struct client_window *window;

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

static void destroy_display(struct client_display *disp)
{
	if (disp->gbm.device)
		gbm_device_destroy(disp->gbm.device);

	if (disp->gbm.drm_fd >= 0)
		close(disp->gbm.drm_fd);

	if (disp->egl.context != EGL_NO_CONTEXT)
		eglDestroyContext(disp->egl.display, disp->egl.context);

	if (disp->egl.display != EGL_NO_DISPLAY)
		eglTerminate(disp->egl.display);

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
	clv_debug("count_configs = %d", count_configs);

	configs = calloc(count_configs, sizeof(*configs));
	if (!configs)
		return -ENOMEM;

	if (!eglChooseConfig(disp->egl.display, attribs, configs,
			     count_configs, &count_matched)
	    || !count_matched) {
		clv_err("cannot select appropriate configs.");
		goto out1;
	}
	clv_debug("count_matched = %d", count_matched);

	if (!visual_ids || count_ids == 0)
		config_index = 0;

	for (i = 0; config_index == -1 && i < count_ids; i++) {
		config_index = match_config_to_visual(disp->egl.display,
						      visual_ids[i],
						      configs,
						      count_matched);
		clv_debug("config_index = %d i = %d count_ids = %d",
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
		fprintf(stderr, "Failed to create EGLDisplay\n");
		goto error;
	}

	if (eglInitialize(disp->egl.display, &major, &minor) == EGL_FALSE) {
		fprintf(stderr, "Failed to initialize EGLDisplay\n");
		goto error;
	}

	if (egl_choose_config(disp, opaque_attribs, &format, 1,
			      &egl_config, &vid) < 0) {
		fprintf(stderr, "failed to choose config\n");
		goto error;
	}
	printf("vid = %08X\n", vid);

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		fprintf(stderr, "Failed to bind OpenGL ES API\n");
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

static void redraw(void *data);

static s32 client_event_cb(s32 fd, u32 mask, void *data)
{
	static s32 status = 0;
	struct ipc_cmd cmd, s;
	struct client_display *disp = data;
	struct client_window *window = disp->window;

	memset(&cmd, 0, sizeof(cmd));
	memset(&s, 0, sizeof(s));
	if (clv_recv(disp->sock, &cmd, sizeof(cmd)) < 0) {
		close(disp->sock);
		disp->exit = 1;
		return 0;
	}
	memcpy(&window->cmd, &cmd, sizeof(cmd));
	switch (cmd.status) {
	case 0:
		printf("link established. 0x%08lX\n", (u64)(cmd.link));
		status = 1;/* create surface */
		s.status = status;
		s.link = cmd.link;
		s.x = window->x;
		s.y = window->y;
		s.w = window->w;
		s.h = window->h;
		printf("create surface %d,%d %ux%u\n", s.x, s.y, s.w, s.h);
		clv_send(disp->sock, &s, sizeof(s)); /* create surface */
		break;
	case 1:
		status = 2;
		s.status = status;
		s.link = cmd.link;
		s.surface = cmd.surface;
		printf("surface created. 0x%08lX\n", (u64)cmd.surface);
		s.x = window->x;
		s.y = window->y;
		s.w = window->w;
		s.h = window->h;
		s.stride = window->buf.stride;
		s.fd = window->buf.fd;
		printf("import dmabuf %ux%u\n", s.w, s.h);
		/* import dma buffer */
		clv_send(disp->sock, &s, sizeof(s));
		clv_send_fd(disp->sock, s.fd);
		break;
	case 2:
		printf("dmabuf imported 0x%08lX\n", (u64)cmd.buf);
		window->buf.buf_handle = cmd.buf;
		redraw(window);
		break;
	case 3:
		if (status != 3) {
			printf("drop\n");
			return 0;
		}
		if (window->flip_occur == 0) {
			window->flip_occur++;
			printf("buffer complete\n");
		}
		break;
	case 4:
		printf("view created 0x%08lX\n", (u64)cmd.view);
		status = 3;
		break;
	default:
		break;
	}
	return 0;
}

static struct client_display *create_client_display(const char *node)
{
	struct client_display *disp;

	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return NULL;

	disp->gbm.drm_fd = -1;
	disp->sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	assert(disp->sock > 0);
	clv_socket_nonblock(disp->sock);
	assert(clv_socket_connect(disp->sock, "test_dma_buf") == 0);
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
static s32 window_set_up_gl(struct client_window *window)
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

static struct client_window *create_client_window(struct client_display *disp,
						  s32 x, s32 y, u32 w, u32 h)
{
	s32 i, ret;
	struct client_window *win;

	win = calloc(1, sizeof(*win));
	if (!win)
		return NULL;

	win->x = 0;
	win->y = 0;
	win->w = w;
	win->h = h;
	win->buf.w = win->w;
	win->buf.stride = win->w * 4;
	win->buf.h = win->h;
	ret = create_dmabuf(disp, &win->buf);
	assert(ret == 0);

	assert(window_set_up_gl(win) == 0);

	win->flip_occur = 1;

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
static void
render(struct client_window *window, struct dma_buf *buffer)
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
	u64 time_ms;

	gettimeofday(&tv, NULL);
	time_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	/* Split time_ms in repeating windows of [0, iteration_ms) and map them
	 * to offsets in the [-0.5, 0.5) range. */
	offset = (time_ms % iteration_ms) / (float) iteration_ms - 0.5;

	/* Direct all GL draws to the buffer through the FBO */
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->gl_fbo);

	glViewport(0, 0, window->w, window->h);

	glUniform1f(window->gl.offset_uniform, offset);

	glClearColor(0.0,0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(window->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(window->gl.color, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.color);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.color);
}

static void redraw(void *data)
{
	struct client_window *window = data;
	struct client_display *disp = window->disp;
	struct dma_buf *buffer;
	static s32 first = 1;
	struct ipc_cmd s;
	static s32 f = 1;

	memset(&s, 0, sizeof(s));
	assert(clv_event_source_timer_update(disp->repaint_event, 8, 667) ==0);
	buffer = &window->buf;
	render(window, buffer);
	glFlush();

	if (!window->flip_occur) {
		assert(clv_event_source_timer_update(disp->repaint_event, 8, 667) ==0);
		printf("not attach ...\n");
		return;
	}
	memcpy(&s, &window->cmd, sizeof(s));
	s.status = 3;
	s.buf = buffer->buf_handle;
	if (f) {
		printf("attach buffer 0x%08lX\n", (u64)buffer->buf_handle);
		clv_send(disp->sock, &s, sizeof(s)); /* attach buffer */
		f--;
	}
	window->flip_occur--;
	if (first) {
		memcpy(&s, &window->cmd, sizeof(s));
		s.status = 4;
		printf("create view %d,%d %ux%u\n", s.x, s.y, s.w, s.h);
		clv_send(disp->sock, &s, sizeof(s)); /* create view */
		first--;
	}
}

static s32 repaint_cb(void *data)
{
	struct client_display *disp = data;
	struct client_window *window = disp->window;

	redraw(window);
	return 0;
}

void run_client(s32 argc, char **argv)
{
	char const *drm_renderer_node = "/dev/dri/renderD128";
	struct client_display *display;
	struct client_window *win;

	if (argc < 6) {
		usage();
		return;
	}

	display = create_client_display(drm_renderer_node);
	if (!display) {
		clv_err("create client_display failed.");
		return;
	}

	win = create_client_window(display, atoi(argv[2]), atoi(argv[3]),
				   atoi(argv[4]), atoi(argv[5]));
	if (!win) {
		clv_err("create client display failed.");
		return;
	}

	printf("%s(): %d\n", __func__, __LINE__);
	display->window = win;
	win->disp = display;
	display->loop = clv_event_loop_create();
	printf("%s(): %d\n", __func__, __LINE__);
	assert(display->loop);
	display->sock_event = clv_event_loop_add_fd(display->loop,
						    display->sock,
						    CLV_EVT_READABLE,
						    client_event_cb,
						    display);
	assert(display->sock_event);
	display->repaint_event = clv_event_loop_add_timer(display->loop,
							  repaint_cb,
							  display);
	assert(display->repaint_event);

	run_client_event_loop(display);
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
struct server_display;

struct client {
	struct clv_surface *surface;
	struct clv_view *view;
	struct list_head buffers;
	struct list_head link;
	struct clv_event_source *event;
	struct server_display *disp;
	s32 sock;
};

struct server_display {
	struct clv_compositor c;
	struct clv_output output;
	struct clv_event_loop *loop;
	struct clv_event_source *sock_event;
	struct clv_event_source *repaint_event;
	struct list_head clients;

	s32 exit;
	s32 sock;

	Display *dpy;
	Window win;
	u32 w, h;
};

/*
struct ipc_cmd {
	void *link;
	void *surface;
	void *view;
	void *buf;
	u32 status;
	s32 x, y;
	u32 w, h;
	s32 fd;
	void *buf_handle;
};
*/

static s32 client_cb(s32 fd, u32 mask, void *data)
{
	struct client *client = data;
	struct server_display *disp = client->disp;
	struct ipc_cmd cmd, s;
	struct clv_buffer *buf;
	s32 dmabuf_fd;

	memset(&cmd, 0, sizeof(cmd));
	memset(&s, 0, sizeof(s));

	if (clv_recv(fd, &cmd, sizeof(cmd)) < 0) {
		close(fd);
		list_del(&client->link);
		free(client);
		/* TODO */
		return 0;
	}

	switch (cmd.status) {
	case 1:
		printf("creating surface\n");
		client->surface = calloc(1, sizeof(*(client->surface)));
		assert(client->surface);
		client->surface->c = &disp->c;
		client->surface->is_opaque = 0;
		client->surface->view = NULL;
		client->surface->w = cmd.w;
		client->surface->h = cmd.h;
		clv_region_init_rect(&client->surface->damage, 0, 0,
				     client->surface->w,
				     client->surface->h);
		clv_region_init_rect(&client->surface->opaque, 0, 0,
				     client->surface->w,
				     client->surface->h);
		clv_signal_init(&client->surface->destroy_signal);
		memcpy(&s, &cmd, sizeof(s));
		s.surface = client->surface;
		clv_send(fd, &s, sizeof(s));
		break;
	case 2:
		printf("importing dma buf\n");
		dmabuf_fd = 0;
		dmabuf_fd = clv_recv_fd(fd);
		printf("dmabuf_fd = %d\n", dmabuf_fd);
		assert(dmabuf_fd > 0);
		buf = disp->c.renderer->import_dmabuf(
					  &disp->c, dmabuf_fd, cmd.w, cmd.h,
					  cmd.stride, CLV_PIXEL_FMT_ARGB8888,
					  DRM_FORMAT_ARGB8888);
		assert(buf);
		memcpy(&s, &cmd, sizeof(s));
		s.status = 2;
		s.buf = buf;
		clv_send(fd, &s, sizeof(s));
		break;
	case 3:
		printf("<<<<<<<< server attaching dma buf >>>>>>>>>>>>\n");
		buf = cmd.buf;
		disp->c.renderer->attach_buffer(cmd.surface, buf);
		break;
	case 4:
		printf("creating view...\n");
		client->view = calloc(1, sizeof(*(client->view)));
		if (!client->view)
			return -ENOMEM;
		client->view->surface = cmd.surface;
		client->view->surface->view = client->view;
		client->view->type = CLV_VIEW_TYPE_PRIMARY;
		client->view->area.pos.x = cmd.x;
		client->view->area.pos.y = cmd.y;
		client->view->area.w = cmd.w;
		client->view->area.h = cmd.h;
		client->view->plane = &disp->c.primary_plane;
		client->view->alpha = 1.0f;
		list_add_tail(&client->view->link, &disp->c.views);
		memcpy(&s, &cmd, sizeof(s));
		s.status = 4;
		s.view = client->view;
		clv_send(fd, &s, sizeof(s));
		printf("view created.\n");
		break;
	}
	return 0;
}

static s32 server_cb(s32 fd, u32 mask, void *data)
{
	struct client *client;
	struct server_display *disp = data;
	s32 sock = clv_socket_accept(fd);
	struct ipc_cmd s;

	memset(&s, 0, sizeof(s));
	assert(sock);
	client = calloc(1, sizeof(*client));
	if (!client)
		return -ENOMEM;

	client->sock = sock;
	client->disp = disp;
	client->event = clv_event_loop_add_fd(disp->loop, client->sock,
					      CLV_EVT_READABLE,
					      client_cb,
					      client);
	assert(client->event);

	s.status = 0;
	s.link = client;
	list_add_tail(&client->link, &disp->clients);
	INIT_LIST_HEAD(&client->buffers);
	clv_send(client->sock, &s, sizeof(s));
	return 0;
}

static s32 server_repaint_cb(void *data)
{
	struct server_display *disp = data;
	struct clv_compositor *c = &disp->c;
	struct ipc_cmd s;
	struct client *client;
	
	memset(&s, 0, sizeof(s));
	if (list_empty(&disp->clients)) {
		assert(clv_event_source_timer_update(disp->repaint_event, 16, 667) ==0);
		return 0;
	}
		
	c->renderer->repaint_output(&disp->output);
	assert(clv_event_source_timer_update(disp->repaint_event, 16, 667) ==0);
	list_for_each_entry(client, &disp->clients, link) {
		s.status = 3;
		s.surface = client->surface;
		s.view = client->view;
		printf("send buf complete ...\n");
		clv_send(client->sock, &s, sizeof(s));
	}
	return 0;
}

struct clv_mode mode = {
      .w = 800,
      .h = 600,
};

static struct server_display *create_server_display(u32 width, u32 height)
{
	struct server_display *disp;
	Window root, win;
	XSetWindowAttributes attr;
	XVisualInfo *visinfo, vistemplate;
	s32 count_visuals, scrnum;
	u32 mask;
	EGLint vid, vid1;

	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return NULL;

	disp->dpy = XOpenDisplay(NULL);
	assert(disp->dpy);
	INIT_LIST_HEAD(&disp->clients);
	disp->w = width;
	disp->h = height;
	disp->exit = 0;
	disp->loop = clv_event_loop_create();
	assert(disp->loop);
	disp->sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	assert(disp->sock > 0);
	clv_socket_nonblock(disp->sock);
	assert(clv_socket_bind_listen(disp->sock, "test_dma_buf") == 0);
	disp->sock_event = clv_event_loop_add_fd(disp->loop, disp->sock,
						 CLV_EVT_READABLE,
						 server_cb,
						 disp);
	assert(disp->sock_event);
	disp->repaint_event = clv_event_loop_add_timer(disp->loop,
						       server_repaint_cb,
						       disp);
	assert(clv_event_source_timer_update(disp->repaint_event, 16, 667) ==0);
	assert(disp->repaint_event);
	memset(&disp->c, 0, sizeof(disp->c));
	memset(&disp->output, 0, sizeof(disp->output));
	INIT_LIST_HEAD(&disp->c.views);
	INIT_LIST_HEAD(&disp->c.outputs);
	disp->output.c = &disp->c;
	disp->output.render_area.pos.x = 0;
	disp->output.render_area.pos.y = 0;
	disp->output.render_area.w = width;
	disp->output.render_area.h = height;
	disp->output.current_mode = &mode;
	disp->output.current_mode->w = width;
	disp->output.current_mode->h = height;
	list_add_tail(&disp->output.link, &disp->c.outputs);
	assert(disp->sock_event);
	assert(renderer_create(&disp->c, NULL, 0, 0, disp->dpy, &vid) == 0);
	printf("vid = %u\n", vid);

	scrnum = DefaultScreen(disp->dpy);
	root = RootWindow(disp->dpy, scrnum);
	vistemplate.visualid = vid;
	visinfo = XGetVisualInfo(disp->dpy, VisualIDMask, &vistemplate,
				 &count_visuals);
	if (!visinfo || count_visuals < 1) {
		printf("cannot get X visual\n");
		exit(1);
	}

	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(disp->dpy, root, visinfo->visual,
					AllocNone);
	attr.event_mask = StructureNotifyMask | ExposureMask;
	mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
	win = XCreateWindow(disp->dpy, root, 0, 0, width, height,
			    0, visinfo->depth, InputOutput,
			    visinfo->visual, mask, &attr);
	{
		XSizeHints sizehints;
		sizehints.x = 0;
		sizehints.y = 0;
		sizehints.width  = width;
		sizehints.height = height;
		sizehints.flags = USSize | USPosition;
		XSetNormalHints(disp->dpy, win, &sizehints);
		XSetStandardProperties(disp->dpy, win, "TestDMABUF","TestDMABF",
                              None, (char **)NULL, 0, &sizehints);
	}
	XFree(visinfo);
	disp->win = win;
	XMapWindow(disp->dpy, win);
	disp->c.renderer->output_create(&disp->output, disp->win, NULL, NULL,0,
					&vid1);
	printf("vid1 = %u\n", vid1);
	assert(vid == vid1);

	return disp;
}

static void run_server_event_loop(struct server_display *disp)
{
	while (!disp->exit) {
		clv_event_loop_dispatch(disp->loop, -1);
	}
}

void run_server(s32 argc, char **argv)
{
	struct server_display *disp;

	if (argc < 4) {
		usage();
		return;
	}

	disp = create_server_display(atoi(argv[2]), atoi(argv[3]));
	assert(disp);
	run_server_event_loop(disp);
}

s32 main(s32 argc, char **argv)
{
	if (argc < 2) {
		usage();
		return -1;
	}

	if (!strcmp(argv[1], "server")) {
		run_server(argc, argv);
	} else if (!strcmp(argv[1], "client")) {
		run_client(argc, argv);
	}

	return 0;
}

