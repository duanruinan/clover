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

void usage(void)
{
	printf("clover_server [options]\n");
	printf("\toptions:\n");
	printf("\t\t-d, --daemon\n");
	printf("\t\t\tRun server as daemon\n");
	printf("\t\t-n, --drm-node=/dev/cardX\n");
	printf("\t\t\tDRM device node. e.g. /dev/dri/card0 as default.\n");
}

char server_short_options[] = "dn:";

struct option server_options[] = {
	{"daemon", 0, NULL, 'd'},
	{"drm-node", 1, NULL, 'n'},
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
	void *display = data;

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

s32 main(s32 argc, char **argv)
{
	s32 ch;
	void *display;
	struct clv_event_loop *loop;
	struct clv_event_source *sig_int_source, *sig_tem_source;
	
	while ((ch = getopt_long(argc, argv, server_short_options,
				 server_options, NULL)) != -1) {
		switch (ch) {
		case 'n':
			strcpy(drm_node, optarg);
			break;
		case 'd':
			run_as_daemon = 1;
			break;
		default:
			usage();
			return -1;
		}
	}

	clv_info("Run as daemon: %s", run_as_daemon ? "Y" : "N");
	clv_info("DRM device node: %s", drm_node);

	if (run_as_daemon)
		run_daemon();

	display = clv_display_create();
	if (!display) {
		clv_err("cannot create clover display.");
		return -1;
	}

	loop = clv_display_get_event_loop(display);
	assert(loop);

	sig_int_source = clv_event_loop_add_signal(loop, SIGINT,
						   signal_event_proc,
						   display);

	sig_tem_source = clv_event_loop_add_signal(loop, SIGTERM,
						   signal_event_proc,
						   display);

	clv_display_run(display);

	clv_event_source_remove(sig_int_source);
	clv_event_source_remove(sig_tem_source);
	
	clv_display_destroy(display);
	

	return 0;
}

