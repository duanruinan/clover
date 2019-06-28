#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_region.h>
#include <clover_event.h>
#include <compositor.h>

struct clv_box desktops[] = {
	{
		.p1 = {
			.x = 0,
			.y = 0,
		},
		.p2 = {
			.x = 2560,
			.y = 1440,
		},
	},
	{
		.p1 = {
			.x = 2560,
			.y = 0,
		},
		.p2 = {
			.x = 2560+1600,
			.y = 900,
		},
	},
};

s32 main(s32 argc, char **argv)
{
	struct clv_compositor_config config;
	struct clv_box *box;
	struct clv_server server;
	struct clv_compositor c;

	memset(&c, 0, sizeof(c));
	INIT_LIST_HEAD(&c.views);
	INIT_LIST_HEAD(&c.outputs);
	INIT_LIST_HEAD(&c.pending_outputs);
	INIT_LIST_HEAD(&c.heads);
	INIT_LIST_HEAD(&c.planes);
	list_add_tail(&c.root_plane.link, &c.planes);
	server.loop = clv_event_loop_create();
	strcpy(config.dev_node, "/dev/dri/card0");
	config.mode = CLV_DESKTOP_MODE_EXTENDED;
	clv_region_init_boxes(&config.canvas, desktops, 2);
	box = clv_region_extents(&config.canvas);
	clv_info("config.canvas: %d,%d %d,%d", box->p1.x, box->p1.y,
			box->p2.x, box->p2.y);
	server.c = &c;
	c.server = &server;
	drm_backend_create(&c, &config);
	do {
		clv_event_loop_dispatch(server.loop, -1);
	} while (1);
	return 0;
}

