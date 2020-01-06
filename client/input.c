#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/kd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <linux/input.h>
#include <libudev.h>
#include <clover_event.h>
#include <clover_shm.h>
#include <clover_ipc.h>
#include <clover_log.h>
#include <clover_utils.h>
#include <clover_protocal.h>

#define CURSOR_MAX_WIDTH 64
#define CURSOR_MAX_HEIGHT 64

#define CURSOR_ACCEL_THRESHOLD_A 20
#define CURSOR_ACCEL_THRESHOLD_B 30
#define CURSOR_ACCEL_THRESHOLD_C 40
#define CURSOR_ACCEL_THRESHOLD_D 50

static s32 has_kbd = 0;

enum input_type {
	INPUT_TYPE_UNKNOWN = 0,
	INPUT_TYPE_MOUSE,
	INPUT_TYPE_KBD,
};

struct shm_buf {
	u32 w, h;
	u32 stride;
	struct clv_shm shm;
	u64 id;
};

struct input_display;

struct input_device {
	s32 fd;
	enum input_type type;
	char name[256];
	struct clv_event_source *input_source;
	struct input_display *disp;
	struct list_head link;
};

struct input_client {
	s32 sock;
	struct clv_event_source *client_source;
	struct input_cmd cmd_rx;
	struct input_display *disp;
	struct list_head link;
};

struct input_display {
	struct udev *udev;
	struct udev_monitor *udev_monitor;
	struct clv_shm accel_fac_shm;
	float *accel_fac;
	struct clv_rect global_area[MAX_DESKTOP_NR];
	struct clv_rect area[MAX_DESKTOP_NR];
	s32 count_areas;
	s32 area_map[MAX_DESKTOP_NR];
	u32 mode;

	s32 created;

	struct clv_event_source *udev_source;
	struct clv_event_source *clv_source;
	struct clv_event_source *server_source;
	struct clv_event_source *repaint_source;
	struct clv_event_source *timeout_source;
	s32 repaint_damage;
	s32 server_sock;
	struct list_head clients;

	struct clv_event_loop *loop;

	struct list_head devs;

	struct input_event *buffer;
	s32 buffer_sz;

	struct clv_input_event *tx_buf;
	s32 tx_buf_sz;

	u32 abs_x, abs_y; /* cursor pos */
	s32 screen_x, screen_y; /* cursor pos */
	s32 hot_x, hot_y;
	s32 hot_x_pending, hot_y_pending;
	s32 cursor_ack_pending;
	s32 cursor_pending;
	s32 need_wait_bo_complete;

	s32 clv_sock;

	u8 *ipc_rx_buf;
	u32 ipc_rx_buf_sz;

	struct shm_buf bufs[2];
	s32 back_buf;

	struct clv_surface_info s;
	struct clv_view_info v;
	struct clv_bo_info b;
	struct clv_commit_info c;
	struct clv_shell_info si;

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

	u8 *shell_tx_cmd_t;
	u8 *shell_tx_cmd;
	u32 shell_tx_len;

	u64 link_id;

	s32 run;
};

static void input_device_destroy(struct input_device *dev)
{
	if (!dev)
		return;

	clv_debug("Destroy input %s", dev->name);
	list_del(&dev->link);
	if (dev->input_source)
		clv_event_source_remove(dev->input_source);
	close(dev->fd);
	free(dev);
}

static void destroy_shmbuf(struct input_display *disp, u32 index)
{
	struct shm_buf *buffer = &disp->bufs[index];

	clv_shm_release(&buffer->shm);
}

static void input_client_destroy(struct input_client *client)
{
	clv_event_source_remove(client->client_source);
	close(client->sock);
	list_del(&client->link);
	free(client);
}

static void redraw_cursor(struct input_display *disp, s32 damage, u8 *data,
			  u32 w, u32 h);

static s32 client_sock_cb(s32 fd, u32 mask, void *data)
{
	struct input_client *client = data;
	struct input_display *disp = client->disp;
	s32 ret;

	ret = clv_recv(fd, &client->cmd_rx, sizeof(client->cmd_rx));
	if (ret == -1) {
		clv_err("input client exit.");
		input_client_destroy(client);
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive input client cmd");
		input_client_destroy(client);
		return -1;
	}

	switch (client->cmd_rx.type) {
	case INPUT_CMD_TYPE_SET_CURSOR:
		clv_debug("Receive set cursor cmd >>> %d,%d %ux%u",
			  client->cmd_rx.c.cursor.hot_x,
			  client->cmd_rx.c.cursor.hot_y,
			  client->cmd_rx.c.cursor.w,
			  client->cmd_rx.c.cursor.h);
		disp->hot_x_pending = client->cmd_rx.c.cursor.hot_x;
		disp->hot_y_pending = client->cmd_rx.c.cursor.hot_y;
		redraw_cursor(disp, 1, (u8 *)client->cmd_rx.c.cursor.data,
			      client->cmd_rx.c.cursor.w,
			      client->cmd_rx.c.cursor.h);
		break;
	case INPUT_CMD_TYPE_SET_CURSOR_RANGE:
		clv_debug("Set Cursor range >>>");
		disp->count_areas = client->cmd_rx.c.range.count_rects;
		memcpy(disp->area_map, client->cmd_rx.c.range.map,
			sizeof(s32) * MAX_DESKTOP_NR);
		memcpy(disp->global_area, client->cmd_rx.c.range.global_area,
			sizeof(struct clv_rect) * MAX_DESKTOP_NR);
		break;
	default:
		printf("Unkown cursor cmd! %u\n", client->cmd_rx.type);
		return -1;
	}

	return 0;
}

static struct input_client *input_client_create(struct input_display *disp,
						s32 sock)
{
	struct input_client *client = calloc(1, sizeof(*client));

	if (!client)
		return NULL;

	client->disp = disp;
	client->sock = sock;
	client->client_source = clv_event_loop_add_fd(disp->loop,
						      sock,
						      CLV_EVT_READABLE,
						      client_sock_cb,
						      client);
	list_add_tail(&client->link, &disp->clients);
	return client;
}

static void input_display_destroy(struct input_display *disp)
{
	struct input_device *dev, *next;
	struct input_client *client, *n;
	s32 i;

	if (!disp)
		return;

	list_for_each_entry_safe(client, n, &disp->clients, link) {
		input_client_destroy(client);
	}

	if (disp->repaint_source)
		clv_event_source_remove(disp->repaint_source);

	if (disp->timeout_source)
		clv_event_source_remove(disp->timeout_source);

	if (disp->server_source)
		clv_event_source_remove(disp->server_source);
	if (disp->server_sock)
		close(disp->server_sock);

	if (disp->clv_source)
		clv_event_source_remove(disp->clv_source);

	if (disp->clv_sock > 0)
		close(disp->clv_sock);

	if (disp->create_surface_tx_cmd_t)
		free(disp->create_surface_tx_cmd_t);
	if (disp->create_surface_tx_cmd);
		free(disp->create_surface_tx_cmd);

	if (disp->create_view_tx_cmd_t)
		free(disp->create_view_tx_cmd_t);
	if (disp->create_view_tx_cmd)
		free(disp->create_view_tx_cmd);

	if (disp->create_bo_tx_cmd_t)
		free(disp->create_bo_tx_cmd_t);
	if (disp->create_bo_tx_cmd)
		free(disp->create_bo_tx_cmd);

	if (disp->commit_tx_cmd_t)
		free(disp->commit_tx_cmd_t);
	if (disp->commit_tx_cmd)
		free(disp->commit_tx_cmd);

	if (disp->shell_tx_cmd_t)
		free(disp->shell_tx_cmd_t);
	if (disp->shell_tx_cmd)
		free(disp->shell_tx_cmd);

	if (disp->terminate_tx_cmd_t)
		free(disp->terminate_tx_cmd_t);
	if (disp->terminate_tx_cmd)
		free(disp->terminate_tx_cmd);

	if (disp->ipc_rx_buf)
		free(disp->ipc_rx_buf);

	for (i = 0; i < 2; i++)
		destroy_shmbuf(disp, i);

	list_for_each_entry_safe(dev, next, &disp->devs, link) {
		input_device_destroy(dev);
	}

	if (disp->udev_source)
		clv_event_source_remove(disp->udev_source);

	if (disp->udev)
		udev_unref(disp->udev);

	if (disp->udev_monitor)
		udev_monitor_unref(disp->udev_monitor);

	if (disp->buffer)
		free(disp->buffer);

	if (disp->tx_buf)
		free(disp->tx_buf);

	clv_shm_release(&disp->accel_fac_shm);

	free(disp);
}

static enum input_type test_dev(const char *dev)
{
	s32 fd;
	u8 evbit[EV_MAX/8 + 1];
	char buffer[64];
	u32 i;

	clv_debug("begin test %s", dev);
	fd = open(dev, O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0)
		return INPUT_TYPE_UNKNOWN;

	memset(buffer,0, sizeof(buffer));
	ioctl(fd, EVIOCGNAME(sizeof(buffer) - 1), buffer);

#ifndef test_bit
#define test_bit(bit, array)    (array[bit/8] & (1<<(bit%8)))
#endif
	ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit);
	close(fd);

	for (i = 0; i < EV_MAX; i++) {
		if (test_bit(i, evbit)) {
			switch (i) {
			case EV_KEY:
				clv_debug("cap: key");
				break;
			case EV_REL:
				clv_debug("cap: rel");
				break;
			case EV_ABS:
				clv_debug("cap: abs");
				break;
			case EV_MSC:
				clv_debug("cap: msc");
				break;
			case EV_LED:
				clv_debug("cap: led");
				break;
			case EV_SND:
				clv_debug("cap: sound");
				break;
			case EV_REP:
				clv_debug("cap: repeat");
				break;
			case EV_FF:
				clv_debug("cap: feedback");
				break;
			case EV_SYN:
				clv_debug("cap: sync");
				break;
			}
		}
	}
	
	if (test_bit(EV_KEY, evbit) && test_bit(EV_REL, evbit)
			&& test_bit(EV_SYN, evbit)) {
		return INPUT_TYPE_MOUSE;
	} else if(test_bit(EV_KEY, evbit) && test_bit(EV_REP, evbit)
	    && test_bit(EV_LED, evbit)) {
		return INPUT_TYPE_KBD;
	}
	clv_debug("end test %s", dev);
	return INPUT_TYPE_UNKNOWN;
}

static void remove_input_device(struct input_display *disp, const char *devpath)
{
	struct input_device *dev_present, *next;

	list_for_each_entry_safe(dev_present, next, &disp->devs, link) {
		if (!strcmp(dev_present->name, devpath)) {
			clv_debug("Remove %s, type %s", devpath,
				dev_present->type == INPUT_TYPE_MOUSE?"M":"K");
			if (dev_present->type == INPUT_TYPE_KBD) {
				system("rm -f /tmp/kbd_name");
				has_kbd = 0;
			}
			input_device_destroy(dev_present);
			return;
		}
	}
}

static s32 check_mouse_pos(struct input_display *disp, s32 x, s32 y)
{
	s32 i;
	struct clv_rect *rc;
	s32 valid = 0, cnt;

	cnt = disp->count_areas;

	for (i = 0; i < MAX_DESKTOP_NR; i++) {
		if (!cnt)
			break;
		if (disp->area_map[i] == -1) {
			continue;
		}
		rc = &disp->area[i];
		if (   x >= rc->pos.x && x < (rc->pos.x + (s32)(rc->w))
		    && y >= rc->pos.y && y < (rc->pos.y + (s32)(rc->h))){
			valid = 1;
			break;
		}
		cnt--;
	}

	if (valid)
		return i;
	else
		return -1;
}

static void gen_global_pos(struct input_display *disp, s32 cur_screen)
{
	s32 dx, dy;
	u32 abs_dx, abs_dy;
	s32 i;

loop:
	dx = disp->screen_x - disp->area[cur_screen].pos.x;
	dy = disp->screen_y - disp->area[cur_screen].pos.y;
	if (!(dx >= 0 && dy >= 0)) {
		for (i = 0; i < MAX_DESKTOP_NR; i++) {
			if (disp->area_map[i] == -1)
				continue;
			break;
		}

		if (i == MAX_DESKTOP_NR)
			return;

		disp->screen_x = disp->area[i].pos.x;
		disp->screen_y = disp->area[i].pos.y;
		if ((disp->area_map[0] != -1) && disp->area[0].w
				&& disp->area[0].h)
			cur_screen = 0;
		else
			cur_screen = 1;
		goto loop;
	}
	/*
	 *    dx           w
	 * --------  =  --------
         *  abs_dx       abs_w
	 */
	abs_dx = (float)dx * disp->global_area[cur_screen].w
			/ disp->area[cur_screen].w;
	abs_dy = (float)dy * disp->global_area[cur_screen].h
			/ disp->area[cur_screen].h;
	disp->abs_x = disp->global_area[cur_screen].pos.x + abs_dx;
	disp->abs_y = disp->global_area[cur_screen].pos.y + abs_dy;
}

static void reset_mouse_pos(struct input_display *disp)
{
	s32 i;

	for (i = 0; i < MAX_DESKTOP_NR; i++) {
		if (disp->area_map[i] == -1) {
			continue;
		}
		break;
	}

	if (i == MAX_DESKTOP_NR)
		return;

	disp->screen_x = disp->area[i].pos.x;
	disp->screen_y = disp->area[i].pos.y;
	gen_global_pos(disp, i);

	clv_debug("reset cursor: %d, %d", disp->screen_x, disp->screen_y);
	redraw_cursor(disp, 0, NULL, 64, 64);
}

static void send_mouse_pos_manually(struct input_display *disp)
{
	struct clv_input_event event;
	struct input_client *client;
	u32 len = sizeof(event);

	memset(&event, 0, sizeof(event));
	event.type = EV_ABS;
	event.code = ABS_X | ABS_Y;
	event.v.pos.x = disp->abs_x;
	event.v.pos.y = disp->abs_x;
	event.v.pos.dx = 0;
	event.v.pos.dy = 0;

	list_for_each_entry(client, &disp->clients, link) {
		clv_send(client->sock, &len, sizeof(u32));
		clv_send(client->sock, &event, len);
	}
}

static void normalize_mouse_pos(s32 cur_screen,
				struct input_display *disp, s32 dx, s32 dy)
{
	struct clv_rect *rc = &disp->area[cur_screen];
	s32 index;

	index = check_mouse_pos(disp, disp->screen_x + dx,
				disp->screen_y + dy);
	if (index < 0) {
		if (check_mouse_pos(disp, disp->screen_x + dx, disp->screen_y)
				< 0) {
			if ((disp->screen_x + dx)
					>= (rc->pos.x + (s32)(rc->w))) {
				disp->screen_x = rc->pos.x + rc->w - 1;
			} else if ((disp->screen_x + dx) < rc->pos.x) {
				disp->screen_x = rc->pos.x;
			}
		} else {
			disp->screen_x += dx;
		}
		if (check_mouse_pos(disp, disp->screen_x, disp->screen_y + dy)
				< 0) {
			if ((disp->screen_y + dy)
					>= (rc->pos.y + (s32)(rc->h))) {
				disp->screen_y = rc->pos.y + rc->h - 1;
			} else if ((disp->screen_y + dy) < rc->pos.y) {
				disp->screen_y = rc->pos.y;
			}
		} else {
			disp->screen_y += dy;
		}
		index = cur_screen;
	} else {
		disp->screen_x += dx;
		disp->screen_y += dy;
	}
	gen_global_pos(disp, index);
}

static void mouse_move_proc(struct input_display *disp, s32 dx, s32 dy)
{
	s32 cur_screen = 0;

	cur_screen = check_mouse_pos(disp, disp->screen_x, disp->screen_y);

	if (cur_screen < 0) {
		clv_debug("not on any screen %d, %d", disp->screen_x,
			  disp->screen_y);
		reset_mouse_pos(disp);
		return;
	}

	normalize_mouse_pos(cur_screen, disp, dx, dy);

	redraw_cursor(disp, 0, NULL, 64, 64);
}

static void cursor_accel_set(s32 *dx, s32 *dy, float factor)
{
	float f;

	if (factor <= 1.0f)
		return;

	f = factor - 1.0f;

	if ((*dx >= CURSOR_ACCEL_THRESHOLD_D)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_D))
		f = 1 + f;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_C)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_C))
		f = 1 + f / 4 * 3;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_B)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_B))
		f = 1 + f / 4 * 2;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_A)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_A))
		f = 1 + f / 4;

	*dx = *dx * f;
	*dy = *dy * f;
}

static void event_proc(struct input_display *disp, struct input_event *evts,
		       s32 cnt)
{
	s32 src, dst;
	s32 dx, dy;
	struct input_client *client;

	dx = dy = 0;
	dst = 0;

	for (src = 0; src < cnt; src++) {
		switch (disp->buffer[src].type) {
		case EV_SYN:
			if (dx || dy) {
				cursor_accel_set(&dx, &dy, *disp->accel_fac);
				mouse_move_proc(disp, dx, dy);
				disp->tx_buf[dst].type = EV_ABS;
				disp->tx_buf[dst].code = ABS_X | ABS_Y;
				disp->tx_buf[dst].v.pos.x = disp->abs_x;
				disp->tx_buf[dst].v.pos.y = disp->abs_y;
				disp->tx_buf[dst].v.pos.dx = (s16)dx;
				disp->tx_buf[dst].v.pos.dy = (s16)dy;
				dst++;
			}
			break;
		case EV_MSC:
			break;
		case EV_LED:
			break;
		case EV_KEY:
			disp->tx_buf[dst].type = EV_KEY;
			disp->tx_buf[dst].code = disp->buffer[src].code;
			disp->tx_buf[dst].v.value = disp->buffer[src].value;
			dst++;
			break;
		case EV_REP:
			disp->tx_buf[dst].type = EV_REP;
			disp->tx_buf[dst].code = disp->buffer[src].code;
			disp->tx_buf[dst].v.value = disp->buffer[src].value;
			dst++;
			break;
		case EV_REL:
			switch (disp->buffer[src].code) {
			case REL_WHEEL:
				disp->tx_buf[dst].type = EV_REL;
				disp->tx_buf[dst].code = REL_WHEEL;
				disp->tx_buf[dst].v.value =
					disp->buffer[src].value;
				dst++;
				break;
			case REL_X:
				dx = disp->buffer[src].value;
				break;
			case REL_Y:
				dy = disp->buffer[src].value;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	list_for_each_entry(client, &disp->clients, link) {
		u32 len = sizeof(struct clv_input_event) * dst;
		clv_send(client->sock, &len, sizeof(u32));
		/* send raw event */
		clv_send(client->sock, disp->tx_buf, len);
	}
}

static s32 read_input_event(s32 fd, u32 mask, void *data)
{
	struct input_device *dev = data;
	struct input_display *disp = dev->disp;
	s32 ret;

	ret = read(fd, disp->buffer, disp->buffer_sz);
	if (ret <= 0) {
		return ret;
	}
	event_proc(disp, disp->buffer, ret / sizeof(struct input_event));

	return 0;
}

static void add_input_device(struct input_display *disp, const char *devpath)
{
	struct input_device *dev;
	enum input_type type;
	s32 fd;
	char cmd[64];

	type = test_dev(devpath);
	if (type == INPUT_TYPE_UNKNOWN)
		return;
	if (type == INPUT_TYPE_KBD && has_kbd) {
			return;
	}

	fd = open(devpath, O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0) {
		clv_err("cannot open %s, %s", devpath, strerror(errno));
		return;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return;
	dev->fd = fd;
	dev->type = type;
	dev->disp = disp;
	memset(dev->name, 0, 256);
	strcpy(dev->name, devpath);
	dev->input_source = clv_event_loop_add_fd(
					disp->loop,
					dev->fd,
					CLV_EVT_READABLE,
					read_input_event, dev);
	if (!dev->input_source) {
		close(dev->fd);
		free(dev);
		return;
	}

	list_add_tail(&dev->link, &disp->devs);
	clv_debug("Add %s, type %s",
		dev->name,type == INPUT_TYPE_MOUSE?"M":"K");
	if (type == INPUT_TYPE_KBD) {
		memset(cmd, 0, 64);
		sprintf(cmd, "echo %s > /tmp/kbd_name", devpath);
		system(cmd);
		has_kbd = 1;
	}
}

static s32 udev_input_hotplug_event_proc(s32 fd, u32 mask, void *data)
{
	struct udev_device *device;
	struct input_display *disp = data;
	const char *action, *devname;

	device = udev_monitor_receive_device(disp->udev_monitor);
	action = udev_device_get_property_value(device, "ACTION");
	devname = udev_device_get_property_value(device, "DEVNAME");
	if (action && devname) {
		if (!strcmp(action, "add")) {
			add_input_device(disp, devname);
		} else if (!strcmp(action, "remove")) {
			remove_input_device(disp, devname);
		}
	}
	udev_device_unref(device);

	return 0;
}

static s32 timer_proc(void *data)
{
	struct input_display *disp = data;

	//clv_debug("repaint in timer >>");
	clv_event_source_timer_update(disp->repaint_source, 0, 0);
	redraw_cursor(disp, disp->repaint_damage, NULL, 64, 64);
	return 0;
}

static s32 timeout_proc(void *data)
{
	struct input_display *disp = data;

	clv_event_source_timer_update(disp->timeout_source, 0, 0);
	clv_debug("cursor pending timeout !!!");
	disp->cursor_pending = 0;
	return 0;
}

static void schedule_repaint_cursor(struct input_display *disp)
{
	clv_event_source_timer_update(disp->repaint_source, 1, 0);
}

static void redraw_cursor(struct input_display *disp, s32 damage, u8 *data,
			  u32 w, u32 h)
{
	struct shm_buf *buffer;
	u8 *dst;
	s32 i;
	u32 width, height;

	if (!damage && disp->repaint_damage)
		damage = 1;
	//clv_debug("redraw_cursor damage: %d, data: %p, disp: %p",
	//	  damage, data, disp);
	if (damage) {
		buffer = &disp->bufs[disp->back_buf];
		if (data) {
			//clv_debug("copy info cursor buf %lu", buffer->id);
			dst = buffer->shm.map;
			memset(dst, 0, buffer->stride * buffer->h);
			width = MIN(w, 64);
			width = MIN(width, buffer->w);
			height = MIN(h, 64);
			height = MIN(height, buffer->h);
			for (i = 0; i < height; i++) {
				memcpy(dst, data + i * w * 4, width * 4);
				dst += buffer->stride;
			}
		}
	} else {
		buffer = &disp->bufs[1 - disp->back_buf];
	}

	if (disp->need_wait_bo_complete) {
		if (disp->cursor_pending || disp->cursor_ack_pending) {
			if (damage || disp->repaint_damage)
				disp->repaint_damage = 1;
			else
				disp->repaint_damage = 0;
			//clv_debug("schedule repaint");
			schedule_repaint_cursor(disp);
			return;
		}
	} else {
		if (disp->cursor_ack_pending) {
			if (damage || disp->repaint_damage)
				disp->repaint_damage = 1;
			else
				disp->repaint_damage = 0;
			//clv_debug("schedule repaint");
			schedule_repaint_cursor(disp);
			return;
		}
	}

	//clv_debug("commit cursor buf %lu, disp %p", buffer->id, disp);
	disp->c.bo_id = buffer->id;

	disp->c.bo_damage.pos.x = 0;
	disp->c.bo_damage.pos.y = 0;
	if (damage) {
		disp->c.bo_damage.w = buffer->w;
		disp->c.bo_damage.h = buffer->h;
	} else {
		disp->c.bo_damage.w = 0;
		disp->c.bo_damage.h = 0;
	}

	if (damage) {
		//clv_debug("Hot pos pending: %d, %d, disp: %p",
		//				disp->hot_x_pending,
		//				disp->hot_y_pending, disp);
		disp->hot_x = disp->hot_x_pending;
		disp->hot_y = disp->hot_y_pending;
		disp->hot_x_pending = disp->hot_y_pending = 0;
	}

	disp->c.view_x = disp->screen_x;
	disp->c.view_y = disp->screen_y;
	disp->c.view_hot_x = disp->hot_x;
	disp->c.view_hot_y = disp->hot_y;
	//clv_debug("Hot pos: (%d,%d), disp: %p", disp->hot_x, disp->hot_y, disp);

	clv_dup_commit_req_cmd(disp->commit_tx_cmd, disp->commit_tx_cmd_t,
			       disp->commit_tx_len, &disp->c);
	if (damage) {
		disp->back_buf = 1 - disp->back_buf;
		disp->need_wait_bo_complete = 1;
		clv_event_source_timer_update(disp->timeout_source, 200, 0);
	} else {
		disp->need_wait_bo_complete = 0;
	}
	clv_send(disp->clv_sock, disp->commit_tx_cmd, disp->commit_tx_len);
	disp->cursor_pending = 1;
	disp->cursor_ack_pending = 1;
	disp->repaint_damage = 0;
}

static s32 clv_event_proc(s32 fd, u32 mask, void *data)
{
	s32 ret, i;
	struct input_display *disp = data;
	struct clv_tlv *tlv;
	u8 *rx_p;
	u32 flag, length;
	u64 id;
	static s32 bo_0_created = 0;

	ret = clv_recv(fd, disp->ipc_rx_buf, sizeof(*tlv) + sizeof(u32));
	if (ret == -1) {
		clv_err("server exit.");
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive server cmd");
	}
	tlv = (struct clv_tlv *)(disp->ipc_rx_buf + sizeof(u32));
	length = tlv->length;
	rx_p = disp->ipc_rx_buf + sizeof(u32) + sizeof(*tlv);
	flag = *((u32 *)(disp->ipc_rx_buf));
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
			id = clv_client_parse_link_id(disp->ipc_rx_buf);
			if (id == 0) {
				clv_err("create link failed.");
				return -1;
			}
			disp->link_id = id;
			clv_debug("link_id: 0x%08lX", disp->link_id);
			memset(&disp->si, 0, sizeof(disp->si));
			disp->si.cmd = CLV_SHELL_CANVAS_LAYOUT_QUERY;
			clv_dup_shell_cmd(disp->shell_tx_cmd,
					  disp->shell_tx_cmd_t,
					  disp->shell_tx_len, &disp->si);
			clv_send(fd, disp->shell_tx_cmd, disp->shell_tx_len);
		} else if (flag & (1 << CLV_CMD_SHELL_SHIFT)) {
			clv_debug("receive shell event");
			memset(&disp->si, 0, sizeof(disp->si));
			clv_parse_shell_cmd(disp->ipc_rx_buf, &disp->si);
			disp->mode = disp->si.value.layout.mode;
			for (i = 0; i < disp->si.value.layout.count_heads; i++){
				memcpy(&disp->area[i],
				       &disp->si.value.layout.desktops[i],
				       sizeof(struct clv_rect));
			}
			disp->count_areas = disp->si.value.layout.count_heads;
			if (disp->created) {
				reset_mouse_pos(disp);
				send_mouse_pos_manually(disp);
			} else {
				(void)clv_dup_create_surface_cmd(
					disp->create_surface_tx_cmd,
					disp->create_surface_tx_cmd_t,
					disp->create_surface_tx_len,
					&disp->s);
				ret = clv_send(fd, disp->create_surface_tx_cmd,
					       disp->create_surface_tx_len);
				if (ret == -1) {
					clv_err("server exit.");
					return -1;
				} else if (ret < 0) {
					clv_err("failed to create surface");
					return -1;
				}
			}
		} else if (flag & (1 << CLV_CMD_CREATE_SURFACE_ACK_SHIFT)) {
			id = clv_client_parse_surface_id(disp->ipc_rx_buf);
			if (id == 0) {
				clv_err("create surface failed.");
				return -1;
			}
			disp->s.surface_id = id;
			clv_debug("create surface ok, surface_id = 0x%08lX",
				  disp->s.surface_id);
			(void)clv_dup_create_view_cmd(
					disp->create_view_tx_cmd,
					disp->create_view_tx_cmd_t,
					disp->create_view_tx_len,
					&disp->v);
			ret = clv_send(fd, disp->create_view_tx_cmd,
				       disp->create_view_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create view cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_VIEW_ACK_SHIFT)) {
			id = clv_client_parse_view_id(disp->ipc_rx_buf);
			if (id == 0) {
				clv_err("create view failed.");
				return -1;
			}
			disp->v.view_id = id;
			clv_debug("create view ok, view_id: 0x%08lX",
				  disp->v.view_id);
			disp->b.stride = disp->bufs[0].stride;
			strcpy(disp->b.name, disp->bufs[0].shm.name);
			disp->b.surface_id = disp->s.surface_id;
			(void)clv_dup_create_bo_cmd(
					disp->create_bo_tx_cmd,
					disp->create_bo_tx_cmd_t,
					disp->create_bo_tx_len,
					&disp->b);
			ret = clv_send(fd, disp->create_bo_tx_cmd,
				       disp->create_bo_tx_len);
			if (ret == -1) {
				clv_err("server exit.");
				return -1;
			} else if (ret < 0) {
				clv_err("failed to send create bo cmd");
				return -1;
			}
		} else if (flag & (1 << CLV_CMD_CREATE_BO_ACK_SHIFT)) {
			id = clv_client_parse_bo_id(disp->ipc_rx_buf);
			if (id == 0) {
				clv_err("create bo failed.");
				return -1;
			}
			if (!bo_0_created) {
				disp->bufs[0].id = id;
				clv_debug("create bo[0] ok, bo_id: 0x%08lX",
					  disp->bufs[0].id);
				bo_0_created = 1;
				disp->b.stride = disp->bufs[1].stride;
				strcpy(disp->b.name, disp->bufs[1].shm.name);
				disp->b.surface_id = disp->s.surface_id;
				(void)clv_dup_create_bo_cmd(
						disp->create_bo_tx_cmd,
						disp->create_bo_tx_cmd_t,
						disp->create_bo_tx_len,
						&disp->b);
				ret = clv_send(fd, disp->create_bo_tx_cmd,
					       disp->create_bo_tx_len);
				if (ret == -1) {
					clv_err("server exit.");
					return -1;
				} else if (ret < 0) {
					clv_err("failed to send create bo cmd");
					return -1;
				}
			} else {
				disp->bufs[1].id = id;
				clv_debug("create bo[1] ok, bo_id: 0x%08lX",
					  disp->bufs[1].id);
				clv_debug("Start to draw.");
				redraw_cursor(disp, 1, NULL, 64, 64);
				disp->created = 1;
			}
		} else if (flag & (1 << CLV_CMD_COMMIT_ACK_SHIFT)) {
			// clv_debug("receive commit ack.");
			clv_client_parse_commit_ack_cmd(disp->ipc_rx_buf);
			disp->cursor_ack_pending = 0;
		} else if (flag & (1 << CLV_CMD_BO_COMPLETE_SHIFT)) {
			//clv_debug("receive bo complete.");
			id = clv_client_parse_bo_complete_cmd(disp->ipc_rx_buf);
			disp->cursor_pending = 0;
			clv_event_source_timer_update(disp->timeout_source,0,0);
		} else if (flag & (1 << CLV_CMD_DESTROY_ACK_SHIFT)) {
			clv_debug("receive destroy ack");
		} else if (flag & (1 << CLV_CMD_HPD_SHIFT)) {
			clv_warn("ignore screen HPD message for input");
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

#if 1
static void create_shmbuf(struct input_display *disp, u32 index, u32 w, u32 h)
{
	char name[32];
	struct shm_buf *buffer = &disp->bufs[index];
	struct clv_shm *shm;
	s32 i, j;
	const u32 red = 0xFFFF0000;
	const u32 green = 0xFF00FF00;
	const u32 blue = 0xFF0000FF;
	const u32 yellow = 0xFFFFFF00;
	u32 *map;

	memset(name, 0, 32);
	sprintf(name, "clover_input-%u", index);
	if (w % 4)
		w = w / 4 + 1;
	buffer->w = w;
	buffer->h = h;
	buffer->stride = w * 4;
	clv_shm_init(&buffer->shm, name, buffer->stride * buffer->h, 1);
	shm = &buffer->shm;
	map = (u32 *)(shm->map);
	for (i = 0; i < buffer->w / 2; i++) {
		for (j = 0; j < buffer->h / 2; j++) {
			*(map + i * buffer->stride / 4 + j) = red;
		}
	}
	for (i = 0; i < buffer->w / 2; i++) {
		for (j = buffer->h / 2; j < buffer->h; j++) {
			*(map + i * buffer->stride / 4 + j) = blue;
		}
	}
	for (i = buffer->w / 2; i < buffer->w; i++) {
		for (j = 0; j < buffer->h / 2; j++) {
			*(map + i * buffer->stride / 4 + j) = green;
		}
	}
	for (i = buffer->w / 2; i < buffer->w; i++) {
		for (j = buffer->h / 2; j < buffer->h; j++) {
			*(map + i * buffer->stride / 4 + j) = yellow;
		}
	}
}
#else
static void create_shmbuf(struct input_display *disp, u32 index, u32 w, u32 h)
{
	char name[32];
	struct shm_buf *buffer = &disp->bufs[index];
	struct clv_shm *shm;
	u8 *map;

	memset(name, 0, 32);
	sprintf(name, "clover_input-%u", index);
	if (w % 4)
		w = w / 4 + 1;
	buffer->w = w;
	buffer->h = h;
	buffer->stride = w * 4;
	clv_shm_init(&buffer->shm, name, buffer->stride * buffer->h, 1);
	shm = &buffer->shm;
	map = (u8 *)(shm->map);
	memset(map, 0, buffer->stride * buffer->h);
}
#endif

static void scan_input_devs(struct input_display * disp, const char *input_dir)
{
	char *devname;
	char *filename;
	DIR *dir;
	struct dirent *de;
	enum input_type type;

	dir = opendir(input_dir);
	if(dir == NULL)
		return;

	devname = (char *)malloc(1024);
	memset(devname, 0, 1024);
	strcpy(devname, input_dir);
	filename = devname + strlen(devname);
	*filename++ = '/';

	while ((de = readdir(dir))) {
		if(de->d_name[0] == '.' &&
			(de->d_name[1] == '\0' ||
			(de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;

		strcpy(filename, de->d_name);
		type = test_dev(devname);
		if (type == INPUT_TYPE_UNKNOWN)
			continue;
		add_input_device(disp, devname);
	}

	closedir(dir);
	free(devname);
}

static s32 server_event_cb(s32 fd, u32 mask, void *data)
{
	struct input_display *disp = data;
	s32 sock = clv_socket_accept(fd);

	input_client_create(disp, sock);
	
	return 0;
}

static s32 timer_proc(void *data);

static struct input_display *input_display_create(void)
{
	struct input_display *disp = calloc(1, sizeof(*disp));
	u32 n;
	s32 i;

	if (!disp)
		goto err;

	memset(disp, 0, sizeof(*disp));

	clv_shm_init(&disp->accel_fac_shm, "cursor_accel", 32, 1);
	disp->accel_fac = (float *)disp->accel_fac_shm.map;
	*disp->accel_fac = 1.0f;

	disp->buffer_sz = sizeof(struct input_event) * 1024;
	disp->buffer = (struct input_event *)malloc(disp->buffer_sz);
	if (!disp->buffer)
		goto err;

	memset(disp->buffer, 0, disp->buffer_sz);

	disp->tx_buf_sz = sizeof(struct clv_input_event) * 1024;
	disp->tx_buf = (struct clv_input_event *)malloc(disp->tx_buf_sz);
	if (!disp->tx_buf)
		goto err;

	memset(disp->tx_buf, 0, disp->tx_buf_sz);

	disp->loop = clv_event_loop_create();
	if (!disp->loop)
		goto err;

	disp->udev = udev_new();
	if (!disp->udev)
		goto err;

	disp->udev_monitor = udev_monitor_new_from_netlink(disp->udev, "udev");
	if (!disp->udev_monitor)
		goto err;

	udev_monitor_filter_add_match_subsystem_devtype(disp->udev_monitor,
							"input", NULL);
	udev_monitor_enable_receiving(disp->udev_monitor);

	disp->udev_source = clv_event_loop_add_fd(
					disp->loop,
					udev_monitor_get_fd(disp->udev_monitor),
					CLV_EVT_READABLE,
					udev_input_hotplug_event_proc, disp);
	if (!disp->udev_source)
		goto err;

	disp->repaint_source = clv_event_loop_add_timer(disp->loop,
							timer_proc, disp);
	if (!disp->repaint_source)
		goto err;

	disp->timeout_source = clv_event_loop_add_timer(disp->loop,
							timeout_proc, disp);
	if (!disp->timeout_source)
		goto err;

	INIT_LIST_HEAD(&disp->devs);

	disp->clv_sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	if (disp->clv_sock < 0)
		goto err;

	disp->clv_source = clv_event_loop_add_fd(disp->loop,
						 disp->clv_sock,
						 CLV_EVT_READABLE,
						 clv_event_proc,
						 disp);
	if (!disp->clv_source)
		goto err;

	for (i = 0; i < 2; i++)
		create_shmbuf(disp, i, CURSOR_MAX_WIDTH, CURSOR_MAX_HEIGHT);

	disp->back_buf = 0;

	disp->ipc_rx_buf_sz = 32 * 1024;
	disp->ipc_rx_buf = malloc(disp->ipc_rx_buf_sz);
	if (!disp->ipc_rx_buf)
		goto err;

	disp->s.is_opaque = 0;
	disp->s.damage.pos.x = 0;
	disp->s.damage.pos.y = 0;
	disp->s.damage.w = CURSOR_MAX_WIDTH;
	disp->s.damage.h = CURSOR_MAX_HEIGHT;
	disp->s.opaque.pos.x = 0;
	disp->s.opaque.pos.y = 0;
	disp->s.opaque.w = CURSOR_MAX_WIDTH;
	disp->s.opaque.h = CURSOR_MAX_HEIGHT;
	disp->s.width = CURSOR_MAX_WIDTH;
	disp->s.height = CURSOR_MAX_HEIGHT;
	disp->v.type = CLV_VIEW_TYPE_CURSOR;
	disp->v.area.pos.x = disp->abs_x;
	disp->v.area.pos.y = disp->abs_y;
	disp->v.area.w = CURSOR_MAX_WIDTH;
	disp->v.area.h = CURSOR_MAX_HEIGHT;
	disp->v.alpha = 1.0f;
	disp->v.output_mask = 0x03;
	disp->v.primary_output = 0;
	disp->b.type = CLV_BUF_TYPE_SHM;
	disp->b.fmt = CLV_PIXEL_FMT_ARGB8888;
	disp->b.count_planes = 1;
	disp->b.internal_fmt = 0;
	disp->b.width = disp->s.width;
	disp->b.stride = (disp->s.width % 4) ?
			(disp->s.width / 4 + 1) * 4 : disp->s.width * 4;
	disp->b.height = disp->s.height;
	disp->c.shown = 1;
	disp->c.view_x = disp->abs_x;
	disp->c.view_y = disp->abs_y;
	disp->c.view_width = CURSOR_MAX_WIDTH;
	disp->c.view_height = CURSOR_MAX_HEIGHT;
	disp->c.delta_z = 0;

	disp->create_surface_tx_cmd_t = clv_client_create_surface_cmd(
						&disp->s, &n);
	disp->create_surface_tx_cmd = malloc(n);
	disp->create_surface_tx_len = n;

	disp->create_view_tx_cmd_t = clv_client_create_view_cmd(&disp->v, &n);
	disp->create_view_tx_cmd = malloc(n);
	disp->create_view_tx_len = n;

	disp->create_bo_tx_cmd_t = clv_client_create_bo_cmd(&disp->b, &n);
	disp->create_bo_tx_cmd = malloc(n);
	disp->create_bo_tx_len = n;

	disp->commit_tx_cmd_t = clv_client_create_commit_req_cmd(&disp->c, &n);
	disp->commit_tx_cmd = malloc(n);
	disp->commit_tx_len = n;

	disp->shell_tx_cmd_t = clv_create_shell_cmd(&disp->si, &n);
	disp->shell_tx_cmd = malloc(n);
	disp->shell_tx_len = n;

	disp->terminate_tx_cmd_t = clv_client_create_destroy_cmd(0, &n);
	disp->terminate_tx_cmd = malloc(n);
	disp->terminate_tx_len = n;

	disp->abs_x = disp->abs_y = 0;
	disp->cursor_pending = 0;

	disp->server_sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	unlink("/tmp/CLV_INPUT_SERVER");
	clv_socket_bind_listen(disp->server_sock, "/tmp/CLV_INPUT_SERVER");
	disp->server_source = clv_event_loop_add_fd(disp->loop,
						 disp->server_sock,
						 CLV_EVT_READABLE,
						 server_event_cb,
						 disp);

	INIT_LIST_HEAD(&disp->clients);

	disp->run = 1;

	clv_socket_connect(disp->clv_sock, "/tmp/CLV_SERVER");

	return disp;

err:
	input_display_destroy(disp);
	return NULL;
}

static void input_display_run(struct input_display *disp)
{
	while (disp->run) {
		clv_event_loop_dispatch(disp->loop, -1);
	}
}

s32 main(s32 argc, char **argv)
{
	struct input_display *disp = input_display_create();

	if (!disp)
		return -1;

	scan_input_devs(disp, "/dev/input");
	input_display_run(disp);

	input_display_destroy(disp);
	return 0;
}

