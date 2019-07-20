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
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_ipc.h>
#include <clover_shm.h>
#include <clover_event.h>
#include <clover_compositor.h>

s32 run_as_daemon = 0;
char drm_node[] = "/dev/dri/card0";
char *config_xml = NULL;

struct clv_server server;

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

	//for (i = 0; i < NOFILE; close(i++));

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

static void head_state_changed_cb(struct clv_listener *listener, void *data)
{
	struct clv_compositor *c = data;
	struct clv_backend *b = c->backend;
	struct clv_output *output;
	struct clv_head *head;
	s32 i;

	for (i = 0; i < server.config->count_heads; i++) {
		if (!server.outputs[i]) {
			output = b->output_create(c, &server.config->heads[i]);
			if (!output) {
				clv_err("failed to create output.");
				continue;
			}
			server.outputs[i] = output;
		}
		output = server.outputs[i];
		head = output->head;
		if (!head->changed)
			continue;

		head->retrieve_modes(head);
		if (head->connected) {
			clv_compositor_choose_mode(output,
						   &server.config->heads[i]);
			output->enable(output,
			  &server.config->heads[i].encoder.output.render_area);
			clv_output_schedule_repaint(output, 2);
		} else {
			output->disable(output);
		}
	}
}

static s32 client_sock_cb(s32 fd, u32 mask, void *data)
{
	struct clv_client_agent *agent = data;
	struct clv_tlv *tlv;
	u8 *rx_p;
	u32 flag, length;
	u64 id;
	s32 ret, dmabuf_fd;
	struct clv_surface_info si;
	struct clv_view_info vi;
	struct clv_bo_info bi;
	struct clv_commit_info ci;
	struct clv_buffer *buf;

	ret = clv_recv(fd, agent->ipc_rx_buf, sizeof(*tlv) + sizeof(u32));
	if (ret == -1) {
		clv_err("client exit.");
		client_agent_destroy(agent);
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive client cmd");
		client_agent_destroy(agent);
		return -1;
	}

	tlv = (struct clv_tlv *)(agent->ipc_rx_buf + sizeof(u32));
	length = tlv->length;
	rx_p = agent->ipc_rx_buf + sizeof(u32) + sizeof(*tlv);
	flag = *((u32 *)(agent->ipc_rx_buf));
	ret = clv_recv(fd, rx_p, length);
	if (ret == -1) {
		clv_err("client exit.");
		client_agent_destroy(agent);
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive client cmd");
		client_agent_destroy(agent);
		return -1;
	}

	if (tlv->tag != CLV_TAG_WIN) {
		clv_err("invalid TAG, not a win. 0x%08X", tlv->tag);
		return -1;
	}

	if (flag & (1 << CLV_CMD_CREATE_SURFACE_SHIFT)) {
		ret = clv_server_parse_create_surface_cmd(agent->ipc_rx_buf,
							  &si);
		if (ret < 0) {
			clv_err("failed to parse surface create command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			clv_debug("parse surface create command ok.");
			clv_debug("Surf: %d, %d,%d:%ux%u, %ux%u, %d,%d:%ux%u",
				  si.is_opaque,
				  si.damage.pos.x, si.damage.pos.y,
				  si.damage.w, si.damage.h,
				  si.width, si.height,
				  si.opaque.pos.x, si.opaque.pos.y,
				  si.opaque.w, si.opaque.h);
			agent->surface = clv_surface_create(agent->c, &si,
							    agent);
			assert(agent->surface);
			id = (u64)(agent->surface);
			clv_debug("Surf created 0x%08lX", id);
		}
		clv_dup_surface_id_cmd(agent->surface_id_created_tx_cmd,
				       agent->surface_id_created_tx_cmd_t,
				       agent->surface_id_created_tx_len, id);
		ret = clv_send(fd, agent->surface_id_created_tx_cmd,
			       agent->surface_id_created_tx_len);
		if (ret == -1) {
			clv_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			clv_err("failed to send surface id");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_CREATE_VIEW_SHIFT)) {
		ret = clv_server_parse_create_view_cmd(agent->ipc_rx_buf,
						       &vi);
		if (ret < 0) {
			clv_err("failed to parse view create command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			clv_debug("parse view create command ok.");
			clv_debug("View: %u, %d,%d:%ux%u, %f, 0x%08X, %u",
				  vi.type, vi.area.pos.x, vi.area.pos.y,
				  vi.area.w, vi.area.h,
				  vi.alpha, vi.output_mask,
				  vi.primary_output);
			agent->view = clv_view_create(agent->surface, &vi);
			assert(agent->view);
			id = (u64)(agent->view);
			clv_debug("View created 0x%08lX", id);
		}
		clv_dup_view_id_cmd(agent->view_id_created_tx_cmd,
				    agent->view_id_created_tx_cmd_t,
				    agent->view_id_created_tx_len, id);
		ret = clv_send(fd, agent->view_id_created_tx_cmd,
			       agent->view_id_created_tx_len);
		if (ret == -1) {
			clv_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			clv_err("failed to send view id");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_CREATE_BO_SHIFT)) {
		ret = clv_server_parse_create_bo_cmd(agent->ipc_rx_buf, &bi);
		if (ret < 0) {
			clv_err("failed to parse bo create command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			clv_debug("parse bo create command ok.");
			if (bi.type == CLV_BUF_TYPE_SHM) {
				clv_debug("SHM BO create req: %u, %s, %u:%ux%u "
					  "%lu", bi.fmt, bi.name, bi.width,
					  bi.stride, bi.height, bi.surface_id);
			} else if (bi.type == CLV_BUF_TYPE_DMA) {
				clv_debug("DMA-BUF BO create req: %u, %u, "
					  "%u:%ux%u %lu", bi.fmt,
					  bi.internal_fmt, bi.width, bi.stride,
					  bi.height, bi.surface_id);
				dmabuf_fd = clv_recv_fd(fd);
				clv_debug("receive dma buf fd %d", dmabuf_fd);
				if (dmabuf_fd < 0) {
					clv_err("dmabuf illegal %d", dmabuf_fd);
					getchar();
					client_agent_destroy(agent);
					return -1;
				}
				buf = agent->c->renderer->import_dmabuf(
					agent->c, dmabuf_fd,
					bi.width, bi.height, bi.stride, bi.fmt,
					bi.internal_fmt);
				assert(buf);
				list_add_tail(&buf->link, &agent->buffers);
				id = (u64)buf;
				clv_debug("DMA-BUF BO created 0x%08lX", id);
			}
		}
		clv_dup_bo_id_cmd(agent->bo_id_created_tx_cmd,
				  agent->bo_id_created_tx_cmd_t,
				  agent->bo_id_created_tx_len, id);
		ret = clv_send(fd, agent->bo_id_created_tx_cmd,
			       agent->bo_id_created_tx_len);
		if (ret == -1) {
			clv_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			clv_err("failed to send bo id");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_COMMIT_SHIFT)) {
		ret = clv_server_parse_commit_req_cmd(agent->ipc_rx_buf, &ci);
		if (ret < 0) {
			clv_err("failed to parse commit command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			clv_debug("parse commit command ok.");
			clv_debug("Commit info: 0x%08lX, %d, %d,%d:%ux%u %d",
				  ci.bo_id, ci.shown, ci.view_x, ci.view_y,
				  ci.view_width, ci.view_height, ci.delta_z);

			agent->c->renderer->attach_buffer(
				agent->surface,
				(struct clv_buffer *)(ci.bo_id));

			agent->view->plane = &agent->c->primary_plane;
			clv_view_schedule_repaint(agent->view);
			clv_debug("************ add flip listener");
			clv_surface_add_flip_listener(agent->surface);
			id = 1;
		}
		clv_dup_commit_ack_cmd(agent->commit_ack_tx_cmd,
				    agent->commit_ack_tx_cmd_t,
				    agent->commit_ack_tx_len, id);
		ret = clv_send(fd, agent->commit_ack_tx_cmd,
			       agent->commit_ack_tx_len);
		if (ret == -1) {
			clv_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			clv_err("failed to send commit ack");
			client_agent_destroy(agent);
			return -1;
		}
	} else {
		clv_err("unknown command 0x%08X", flag);
		return -1;
	}

	return 0;
#if 0
	s32 ret, dmabuf_fd;
	struct clv_buffer *buf;
	static s32 f = 1;

	memset(&r, 0, sizeof(r));
	ret = clv_recv(fd, &r, sizeof(r));
	if (ret == -1) {
		clv_err("client exit.");
		client_agent_destroy(agent);
		return -1;
	} else if (ret < 0) {
		clv_err("failed to receive client cmd");
	}

	clv_info("receive from client %d stage %u cmd %u", fd, agent->stage,
		 r.cmd);

	switch (agent->stage) {
	case CLV_CLIENT_STAGE_LINKUP:
		switch (r.cmd) {
		case CLV_CMD_CREATE_SURFACE:
			clv_debug("receive surface create request surf: %ux%u "
				  "view: %d,%d %ux%u",
				  r.s.damage.w, r.s.damage.h,
				  r.v.area.pos.x, r.v.area.pos.y,
				  r.v.area.w, r.v.area.h);
			agent->surface = clv_surface_create(agent->c, &r.s,
							    agent);
			assert(agent->surface);
			memcpy(&s, &r, sizeof(s));
			s.cmd = CLV_CMD_CREATE_SURFACE_ACK;
			s.s.surface_id = (u64)(agent->surface);
			clv_debug("surface created %lu", s.s.surface_id);
			agent->view = clv_view_create(agent->surface, &r.v);
			assert(agent->view);
			s.v.view_id = (u64)(agent->view);
			clv_debug("view created %lu", s.v.view_id);
			clv_send(fd, &s, sizeof(s));
			break;
		case CLV_CMD_CREATE_DMA_BUF_BO:
			clv_debug("receive dma bo create request");
			memcpy(&s, &r, sizeof(s));
			dmabuf_fd = clv_recv_fd(fd);
			clv_debug("receive dma buf fd %d", dmabuf_fd);
			buf = agent->c->renderer->import_dmabuf(
				agent->c, dmabuf_fd, r.buf.width, r.buf.height,
				r.buf.stride, r.buf.fmt,
				r.buf.internal_fmt);
			list_add_tail(&buf->link, &agent->buffers);
			assert(buf);
			s.buf_id = (u64)buf;
			s.cmd = CLV_CMD_CREATE_DMA_BUF_BO_ACK;
			clv_debug("send dma bo create ack");
			clv_send(fd, &s, sizeof(s));
			agent->stage = CLV_CLIENT_STAGE_WORKING;
			break;
		default:
			break;
		}
		break;
	case CLV_CLIENT_STAGE_WORKING:
		switch (r.cmd) {
		case CLV_CMD_CREATE_DMA_BUF_BO:
			clv_debug("receive dma bo create request");
			memcpy(&s, &r, sizeof(s));
			dmabuf_fd = clv_recv_fd(fd);
			clv_debug("receive dma buf fd %d", dmabuf_fd);
			buf = agent->c->renderer->import_dmabuf(
				agent->c, dmabuf_fd, r.buf.width, r.buf.height,
				r.buf.stride, r.buf.fmt,
				r.buf.internal_fmt);
			list_add_tail(&buf->link, &agent->buffers);
			assert(buf);
			s.buf_id = (u64)buf;
			s.cmd = CLV_CMD_CREATE_DMA_BUF_BO_ACK;
			clv_debug("send dma bo create ack");
			clv_send(fd, &s, sizeof(s));
			agent->stage = CLV_CLIENT_STAGE_WORKING;
			break;
		case CLV_CMD_COMMIT:
			clv_debug("receive commit request %lu", r.buf_id);
			if (agent->f) {
				agent->f--;
				agent->c->renderer->attach_buffer(
					agent->surface, (struct clv_buffer *)r.buf_id);
			}
			agent->view->plane = &agent->c->primary_plane;
			clv_view_schedule_repaint(agent->view);
			clv_debug("************ add flip listener");
			clv_surface_add_flip_listener(agent->surface);
			memcpy(&s, &r, sizeof(s));
			s.cmd = CLV_CMD_COMMIT_ACK;
			clv_debug("send commit ack");
			clv_send(fd, &s, sizeof(s));
			break;
		default:
			break;
		}
		break;
	case CLV_CLIENT_STAGE_DESTROYING:
		break;
	default:
		printf("unknown stage %u", agent->stage);
		break;
	}
#endif

	return 0;
}

static s32 server_sock_cb(s32 fd, u32 mask, void *data)
{
	struct clv_server *s = data;
	s32 sock = clv_socket_accept(fd);
	struct clv_client_agent *agent;

	agent = client_agent_create(s, sock, client_sock_cb);
	assert(agent);

	//clv_socket_nonblock(sock);
	clv_dup_linkup_cmd(s->linkid_created_ack_tx_cmd,
			   s->linkid_created_ack_tx_cmd_t,
			   s->linkid_created_ack_tx_len,
			   (u64)agent);
	clv_info("Send link id 0x%08lX", (u64)agent);
	assert(clv_send(sock, s->linkid_created_ack_tx_cmd,
			s->linkid_created_ack_tx_len) == 0);
	clv_info("a new client connected. sock = %d", sock);
	
	return 0;
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	struct clv_client_agent *agent, *next;
	u32 n;

	server.linkid_created_ack_tx_cmd_t
		= clv_server_create_linkup_cmd(0, &n);
	assert(server.linkid_created_ack_tx_cmd_t);
	server.linkid_created_ack_tx_cmd = malloc(n);
	assert(server.linkid_created_ack_tx_cmd);
	server.linkid_created_ack_tx_len = n;
	
	server.hpd_listener.notify = head_state_changed_cb;
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

	INIT_LIST_HEAD(&server.client_agents);
	server.server_sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	assert(server.server_sock > 0);
	//clv_socket_nonblock(server.server_sock);
	unlink("/tmp/CLV_SERVER");
	assert(clv_socket_bind_listen(server.server_sock,
				      "/tmp/CLV_SERVER") == 0);
	server.server_source = clv_event_loop_add_fd(server.loop,
						     server.server_sock,
						     CLV_EVT_READABLE,
						     server_sock_cb,
						     &server);
	assert(server.server_source);

	server.sig_int_source = clv_event_loop_add_signal(server.loop, SIGINT,
							  signal_event_proc,
							  server.display);

	server.sig_tem_source = clv_event_loop_add_signal(server.loop, SIGTERM,
							  signal_event_proc,
							  server.display);
	server.c = clv_compositor_create(server.display);
	clv_compositor_add_heads_changed_listener(server.c,
						  &server.hpd_listener);
	clv_compositor_schedule_heads_changed(server.c);
	clv_display_run(server.display);

	clv_compositor_destroy(server.c);

	clv_event_source_remove(server.sig_int_source);
	clv_event_source_remove(server.sig_tem_source);

	list_for_each_entry_safe(agent, next, &server.client_agents, link) {
		client_agent_destroy(agent);
	}

	if (server.server_source)
		clv_event_source_remove(server.server_source);

	if (server.server_sock > 0)
		close(server.server_sock);
	
	clv_display_destroy(server.display);

	free(server.config);

	return 0;
}

