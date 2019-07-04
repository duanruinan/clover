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
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_event.h>
#include <clover_compositor.h>

s32 run_as_daemon = 0;
char drm_node[] = "/dev/dri/card0";
char *config_xml = NULL;

struct clv_server {
	struct clv_display *display;
	struct clv_event_loop *loop;
	struct clv_event_source *sig_int_source, *sig_tem_source;
	struct clv_config *config;
	struct clv_compositor *c;
	struct clv_listener hpd_listener;
	s32 count_outputs;
	struct clv_output *outputs[8];
} server;

void usage(void)
{
	printf("clover_server [options]\n");
	printf("\toptions:\n");
	printf("\t\t-d, --daemon\n");
	printf("\t\t\tRun server as daemon\n");
	printf("\t\t-n, --drm-node=/dev/cardX\n");
	printf("\t\t\tDRM device node. e.g. /dev/dri/card0 as default.\n");
	printf("\t\t-c, --config=config.xml\n");
}

char server_short_options[] = "dn:c:";

struct option server_options[] = {
	{"daemon", 0, NULL, 'd'},
	{"drm-node", 1, NULL, 'n'},
	{"config", 1, NULL, 'c'},
};

static void run_daemon(void)
{
	s32 pid;
	s32 i;

	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if (pid > 0)
		exit(0);
	else if (pid < 0)
		exit(1);

	setsid();

	pid = fork();
	if (pid > 0) {
		clv_info("clover_server's PID: %d", pid);
		exit(0);
	} else if (pid < 0) {
		exit(1);
	}

	for (i = 0; i < NOFILE; close(i++));

	chdir("/");

	umask(0);

	signal(SIGCHLD, SIG_IGN);
}

static s32 signal_event_proc(s32 signal_number, void *data)
{
	struct clv_display *display = data;

	switch (signal_number) {
	case SIGINT:
		clv_info("Receive SIGINT, exit.");
		clv_display_stop(display);
		break;
	case SIGTERM:
		clv_info("Receive SIGTERM, exit.");
		clv_display_stop(display);
		break;
	default:
		clv_err("Receive unknown signal %d", signal_number);
		return -1;
	}

	return 0;
}

static void clv_server_output_destroy(struct clv_head *head)
{
	struct clv_config *config = server.config;
	struct clv_head_config *head_config = &config->heads[head->index];
	struct clv_output_config *output_config = &head_config->encoder.output;
	u32 index = output_config->index;
	struct clv_output *output;

	if (server.outputs[index]) {
		output = server.outputs[index];
		output->disable(output);
		output->destroy(output);
		server.outputs[index] = NULL;
	}
}

static void clv_server_output_create(struct clv_head *head)
{
	struct clv_compositor *c = server.c;
	u32 max_w, max_h, w, h, x, y;
	struct clv_rect render_area;
	struct clv_output_config *output_cfg;
	struct clv_head_config *head_cfg;
	s32 i;

	x = y = 0;
	for (i = 0; i < server.config->count_heads; i++) {
		head_cfg = &server.config->heads[i];
		output_cfg = &head_cfg->encoder.output;
		if (head_cfg->index != head->index) {
			if (server.outputs[i]) {
				x += output_cfg->render_area.w;
			}
		} else {
			break;
		}
	}

	if (i == server.config->count_heads)
		return;

	max_w = MIN(head_cfg->max_w, output_cfg->max_w);
	max_h = MIN(head_cfg->max_h, output_cfg->max_h);
	clv_debug("max_w = %u max_h = %u", max_w, max_h);

	render_area.pos.x = x;
	render_area.pos.y = y;

	clv_compositor_choose_head_best_size(c, &w, &h, max_w, max_h,
					     head->index);
	clv_debug("The mode chosen: %ux%u", w, h);

	render_area.w = w;
	render_area.h = h;
	server.outputs[output_cfg->index] = c->backend->output_create(c,
								&render_area,
								head_cfg);

	clv_compositor_choose_current_mode(c, w, h, output_cfg->index);
	server.outputs[output_cfg->index]->enable(
		server.outputs[output_cfg->index]
	);
}

static void heads_changed(struct clv_listener *listener, void *data)
{
	struct clv_head *last = NULL, *head;
	struct clv_mode *mode;
	struct clv_compositor *c = data;

	do {
		head = clv_compositor_enumerate_head(c, last);
		if (head) {
			last = head;
			if (head->connected) {
				mode = head->best_mode;
				clv_notice("Head[%d]: %s %ux%u@%.1fMHz",
					   head->index,
					   "connected", mode->w, mode->h,
					   mode->refresh / 1000.0f);
				clv_server_output_create(head);
			} else {
				clv_notice("Head[%d]: %s", head->index,
					   "disconnected");
				clv_server_output_destroy(head);
			}
		}
	} while (head);
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	
	server.hpd_listener.notify = heads_changed;
	while ((ch = getopt_long(argc, argv, server_short_options,
				 server_options, NULL)) != -1) {
		switch (ch) {
		case 'n':
			strcpy(drm_node, optarg);
			break;
		case 'd':
			run_as_daemon = 1;
			break;
		case 'c':
			config_xml = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	clv_info("Run as daemon: %s", run_as_daemon ? "Y" : "N");
	clv_info("DRM device node: %s", drm_node);
	clv_info("Config file: %s", config_xml);

	if (run_as_daemon)
		run_daemon();

	server.config = load_config_from_file(config_xml);
	server.count_outputs = server.config->count_heads;

	server.display = clv_display_create();
	if (!server.display) {
		clv_err("cannot create clover display.");
		free(server.config);
		return -1;
	}

	server.loop = clv_display_get_event_loop(server.display);
	assert(server.loop);

	server.sig_int_source = clv_event_loop_add_signal(server.loop, SIGINT,
							  signal_event_proc,
							  server.display);

	server.sig_tem_source = clv_event_loop_add_signal(server.loop, SIGTERM,
							  signal_event_proc,
							  server.display);
	server.c = clv_compositor_create(server.display);
	clv_compositor_add_heads_changed_listener(server.c,
						  &server.hpd_listener);

	clv_display_run(server.display);

	clv_compositor_destroy(server.c);

	clv_event_source_remove(server.sig_int_source);
	clv_event_source_remove(server.sig_tem_source);
	
	clv_display_destroy(server.display);

	free(server.config);

	return 0;
}

