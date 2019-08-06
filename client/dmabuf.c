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
#include <getopt.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_ipc.h>
#include <clover_shm.h>
#include <clover_event.h>
#include <clover_protocal.h>

u32 frame_cnt = 0;

void usage(void)
{
	printf("external_dmabuf [options]\n");
	printf("\toptions:\n");
	printf("\t\t-s, --nv12\n");
	printf("\t\t\tRun client with NV12 DMA-BUF\n");
	printf("\t\t-f, --nv24\n");
	printf("\t\t\tRun client with NV16 DMA-BUF\n");
	printf("\t\t-n, --dev-node\n");
	printf("\t\t\tDRM device node. e.g. /dev/dri/renderD128 as default.\n");
	printf("\t\t-x, --left=x position\n");
	printf("\t\t-y, --top=y position\n");
	printf("\t\t-w, --width=window width\n");
	printf("\t\t-h, --height=window height\n");
	printf("\t\t-o, --output=primary output\n");
}

char drm_node[] = "/dev/dri/renderD128";
char client_short_options[] = "sfn:w:h:x:y:o:";
u32 win_width, win_height;
s32 win_x, win_y;
s32 is_nv12 = 0;
u32 output_index = 0;

struct option client_options[] = {
	{"nv12", 0, NULL, 's'},
	{"nv24", 0, NULL, 'f'},
	{"drm-node", 1, NULL, 'n'},
	{"width", 1, NULL, 'w'},
	{"height", 1, NULL, 'h'},
	{"left", 1, NULL, 'x'},
	{"top", 1, NULL, 'y'},
	{"output", 1, NULL, 'o'},
};

struct dma_buf {
	u32 fourcc;
	u32 w, h;
	u32 stride;
	s32 fd;
	u64 id;
	u32 size;
	void *map;
};

struct dmabuf_window;

struct client_display {
	struct clv_event_loop *loop;
	struct clv_event_source *sock_event;
	struct clv_event_source *repaint_event;
	struct clv_event_source *collect_event;

	s32 exit;

	struct dmabuf_window *dmabuf_window;
	struct shm_window *shm_window;

	s32 sock;
	
	s32 drm_fd;
};

struct dmabuf_window {
	struct client_display *disp;
	s32 x, y;
	u32 w, h;
	enum clv_pixel_fmt pixel_fmt;
	struct dma_buf buf[2];
	
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

static void destroy_display(struct client_display *disp)
{
	if (disp->drm_fd > 0)
		close(disp->drm_fd);

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

static struct client_display *create_dmabuf_display(const char *node)
{
	struct client_display *disp;

	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return NULL;

	disp->drm_fd = open(drm_node, O_RDWR);
	if (disp->drm_fd < 0) {
		printf("failed to open drm node %s %s", drm_node,
		       strerror(errno));
		return NULL;
	}
	disp->sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	assert(disp->sock > 0);
	//clv_socket_nonblock(disp->sock);
	assert(clv_socket_connect(disp->sock, "/tmp/CLV_SERVER") == 0);

	printf("create client display ok.\n");
	return disp;
}

static struct client_display *create_client_display(const char *hwaccel_node)
{
	return create_dmabuf_display(hwaccel_node);
}

static s32 create_dmabuf(struct client_display *disp, struct dma_buf *buffer)
{
	u32 w_align, h_align;
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_map_dumb map_arg;
	s32 ret;

	w_align = (buffer->w + 16 - 1) & ~(16 - 1);
	h_align = (buffer->h + 16 - 1) & ~(16 - 1);

	memset(&create_arg, 0, sizeof create_arg);
	create_arg.bpp = 8;
	create_arg.width = w_align;
	if (buffer->fourcc == DRM_FORMAT_NV12)
		create_arg.height = h_align * 3 / 2;
	else
		create_arg.height = h_align * 2;
	printf("FOURCC = %u !!!!!!!!!!!!!!!\n", buffer->fourcc);

	ret = drmIoctl(disp->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
	if (ret) {
		printf("failed to create dumb %s\n", strerror(errno));
		printf("drm_fd = %d, width = %u height = %u w = %u h = %u\n",
			disp->drm_fd, create_arg.width, create_arg.height,
			buffer->w, buffer->h);
		exit(1);
	} else {
		printf("create dumb ok\n");
	}

	buffer->size = create_arg.size;
	printf("buffer->size = %u\n", buffer->size);

	memset(&map_arg, 0, sizeof map_arg);
	map_arg.handle = create_arg.handle;
	ret = drmIoctl(disp->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	if (ret)
		printf("failed to map dumb\n");
	else
		printf("map ok\n");

	buffer->map = mmap(NULL, buffer->size, PROT_WRITE,
		       MAP_SHARED, disp->drm_fd, map_arg.offset);
	if (buffer->map == MAP_FAILED)
		printf("failed to map dumb\n");

	ret = drmPrimeHandleToFD(disp->drm_fd, create_arg.handle, 0,
				&buffer->fd);
	if (ret) {
		printf("failed to get dma fd\n");
	} else {
		printf("get dmafd %d\n", buffer->fd);
	}

	return 0;
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
		if (is_nv12)
			win->buf[i].fourcc = DRM_FORMAT_NV12;
		else
			win->buf[i].fourcc = DRM_FORMAT_NV16;
		win->buf[i].w = win->w;
		win->buf[i].h = win->h;
		ret = create_dmabuf(disp, &win->buf[i]);
		assert(ret == 0);
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
	win->v.output_mask = 0x03;
	win->v.primary_output = output_index;
	win->b.type = CLV_BUF_TYPE_DMA;
	if (is_nv12) {
		win->b.fmt = CLV_PIXEL_FMT_NV12;
		win->b.internal_fmt = DRM_FORMAT_NV12;
	} else {
		win->b.fmt = CLV_PIXEL_FMT_NV16;
		win->b.internal_fmt = DRM_FORMAT_NV16;
	}
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
static void render_gpu(struct dmabuf_window *window, struct dma_buf *buffer)
{
	
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

static s32 dmabuf_repaint_cb(void *data)
{
	struct client_display *disp = data;
	struct dmabuf_window *window = disp->dmabuf_window;

	dmabuf_redraw(window);
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

	while ((ch = getopt_long(argc, argv, client_short_options,
				 client_options, NULL)) != -1) {
		switch (ch) {
		case 'n':
			strcpy(drm_node, optarg);
			break;
		case 's':
			is_nv12 = 1;
			break;
		case 'f':
			is_nv12 = 0;
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

	if (is_nv12) {
		clv_info("Client buffer type: NV12 DMA-BUF");
	} else {
		clv_info("Client buffer type: NV16 DMA-BUF");
	}
	clv_info("Render Device: %s", drm_node);
	clv_info("Window rect: %d,%d %ux%u", win_x, win_y, win_width,
		 win_height);

	run_dmabuf_client();

	return 0;
}

