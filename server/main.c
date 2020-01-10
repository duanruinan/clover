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

static u8 common_dbg = 0;

enum timing_select_method timing_choose_mode;

static void set_common_dbg(u32 flags)
{
	common_dbg = flags & 0x0F;
}

#define com_debug(fmt, ...) do { \
	if (common_dbg >= 3) { \
		clv_debug("[COMM] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define com_info(fmt, ...) do { \
	if (common_dbg >= 2) { \
		clv_info("[COMM] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define com_notice(fmt, ...) do { \
	if (common_dbg >= 1) { \
		clv_notice("[COMM] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define com_warn(fmt, ...) do { \
	clv_warn("[COMM] " fmt, ##__VA_ARGS__); \
} while (0);

#define com_err(fmt, ...) do { \
	clv_err("[COMM] " fmt, ##__VA_ARGS__); \
} while (0);

void usage(void)
{
	printf("clover_server [options]\n");
	printf("\toptions:\n");
	printf("\t\t-h, --highvfreq\n");
	printf("\t\t-d, --daemon\n");
	printf("\t\t\tRun server as daemon\n");
	printf("\t\t-n, --drm-node=/dev/cardX\n");
	printf("\t\t\tDRM device node. e.g. /dev/dri/card0 as default.\n");
	printf("\t\t-c, --config=config.xml\n");
}

char server_short_options[] = "hdn:c:";

struct option server_options[] = {
	{"highvfreq", 0, NULL, 'h'},
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
		com_info("clover_server's PID: %d", pid);
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
		com_info("Receive SIGINT, exit.");
		clv_display_stop(display);
		break;
	case SIGTERM:
		com_info("Receive SIGTERM, exit.");
		clv_display_stop(display);
		break;
	default:
		com_err("Receive unknown signal %d", signal_number);
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
	struct clv_client_agent *agent;
	u64 hpd_info = 0;
	char head_status[64];

	for (i = 0; i < server.config->count_heads; i++) {
		if (!server.outputs[i]) {
			clv_debug("=== create backend output %u", i);
			output = b->output_create(c, &server.config->heads[i]);
			if (!output) {
				com_err("failed to create output.");
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
						   &server.config->heads[i],
						   timing_choose_mode);
			com_info("enable output[%d]", output->index);
			output->enable(output,
			  &server.config->heads[i].encoder.output.render_area);
			//printf("Head change schedule repaint\n");
			clv_output_schedule_repaint_reset(output);
			clv_output_schedule_repaint(output, 2);
			sprintf(head_status, "echo \"%ux%u\" > /tmp/head-%u",
				output->current_mode->w,
				output->current_mode->h,
				head->index);
			system(head_status);
			sprintf(head_status, "echo \"%u\" > /tmp/vfreq-%u",
				output->current_mode->refresh,
				head->index);
			system(head_status);
			set_hpd_info(&hpd_info, i, 1);
		} else {
			com_info("disable output[%d]", output->index);
			output->disable(output);
			sprintf(head_status, "echo \"0x0\" > /tmp/head-%u",
				head->index);
			system(head_status);
			sprintf(head_status, "echo \"0\" > /tmp/vfreq-%u",
				head->index);
			system(head_status);
			set_hpd_info(&hpd_info, i, 0);
		}
	}

	list_for_each_entry(agent, &server.client_agents, link) {
		clv_dup_hpd_cmd(agent->hpd_tx_cmd, agent->hpd_tx_cmd_t,
				agent->hpd_tx_len, hpd_info);
		clv_send(agent->sock, agent->hpd_tx_cmd, agent->hpd_tx_len);
	}
}

static void update_layout(struct clv_compositor *c, struct clv_shell_info *si)
{
	struct clv_config *config;
	struct clv_output *output;
	s32 i;
	struct clv_output_config *output_cfg;
	struct clv_rect *rc;
	struct clv_head *head;

	com_debug("update screen layout");
	config = server.config;
	if (si->value.layout.mode != config->mode) {
		com_info("Layout mode chanaged. %s -> %s",
		    config->mode == CLV_DESKTOP_DUPLICATED ? "D" : "E",
		    si->value.layout.mode == CLV_DESKTOP_DUPLICATED ? "D" :"E");
		config->mode = si->value.layout.mode;
	}
	config->count_heads = si->value.layout.count_heads;
	for (i = 0; i < config->count_heads; i++) {
		output = server.outputs[i];
		if (!output)
			continue;
		output_cfg = &config->heads[i].encoder.output;
		rc = &si->value.layout.desktops[i];
		if ((!rc->w && !rc->h) &&
		    (output_cfg->render_area.w && output_cfg->render_area.h)) {
			com_info("disable output[%d]", output->index);
			output->disable(output);
			clv_output_schedule_repaint(output, 2);
			memcpy(&output_cfg->render_area, rc, sizeof(*rc));
			output->changed = 4;
		}
	}

	com_debug("config->count_heads = %d", config->count_heads);
	for (i = 0; i < config->count_heads; i++) {
		com_debug("output: %p", output);
		output = server.outputs[i];
		if (!output)
			continue;
		com_debug("rc->w %u rc->h %u", rc->w, rc->h);
		rc = &si->value.layout.desktops[i];
		if (!rc->w && !rc->h)
			continue;
		output_cfg = &config->heads[i].encoder.output;
		com_debug("rc: %d,%d %ux%u render_area: %d,%d %ux%u",
			rc->pos.x, rc->pos.y, rc->w, rc->h,
			output_cfg->render_area.pos.x,
			output_cfg->render_area.pos.y,
			output_cfg->render_area.w,
			output_cfg->render_area.h);
		if (rc->w != output_cfg->render_area.w ||
		    rc->h != output_cfg->render_area.h ||
		    rc->pos.x != output_cfg->render_area.pos.x ||
		    rc->pos.y != output_cfg->render_area.pos.y) {
			com_info("desktop[%u] changed %d,%d %ux%u -> "
				 "%d,%d %ux%u", i,
				 output_cfg->render_area.pos.x,
				 output_cfg->render_area.pos.y,
				 output_cfg->render_area.w,
				 output_cfg->render_area.h,
				 rc->pos.x,
				 rc->pos.y,
				 rc->w,
				 rc->h);
			com_info("output[%u] changed", output->index); 
		} else {
			com_info("output[%u] not changed", output->index);
			continue;
		}
		if ((!output_cfg->render_area.w &&
		            !output_cfg->render_area.h) &&
		           (rc->w && rc->h)) {
			if (output->current_mode == NULL) {
				com_warn("output's current_mode == NULL!!!!!");
				com_warn("output's current_mode == NULL!!!!!");
				com_warn("output's current_mode == NULL!!!!!");
				head = output->head;
				head->retrieve_modes(head);
				if (!head->connected) {
					com_warn("head %u not connected, "
						 "do nothing.", head->index);
					continue;
				}
				clv_compositor_choose_mode_manually(
							output, rc,
							timing_choose_mode);
				assert(output->current_mode);
			}
			com_info("enable output[%d]", output->index);
			output->enable(output, rc);
			clv_output_schedule_repaint_reset(output);
			output->changed = 4;
			memcpy(&output_cfg->render_area, rc, sizeof(*rc));
			clv_output_schedule_repaint(output, output->changed);
		} else if (output_cfg->render_area.w &&
		           output_cfg->render_area.h){
			memcpy(&output->render_area, rc,
			       sizeof(struct clv_rect));
			output->changed = 4;
			memcpy(&output_cfg->render_area, rc, sizeof(*rc));
			clv_output_schedule_repaint(output, output->changed);
		}
	}
}

static s32 client_sock_cb(s32 fd, u32 mask, void *data)
{
	struct clv_client_agent *agent = data;
	struct clv_tlv *tlv;
	u8 *rx_p, *shell_tx_buf;
	u32 flag, length, f, f1, n;
	u64 id;
	s32 ret, dmabuf_fd;
	struct clv_surface_info si;
	struct clv_view_info vi;
	struct clv_bo_info bi;
	struct clv_commit_info ci;
	struct clv_shell_info shell;
	struct clv_buffer *buf;
	struct timespec t1, t2;
	//struct timespec ts1;
	struct clv_config *config;
	s32 i;

	ret = clv_recv(fd, agent->ipc_rx_buf, sizeof(*tlv) + sizeof(u32));
	if (ret == -1) {
		com_err("client exit.");
		client_agent_destroy(agent);
		return -1;
	} else if (ret < 0) {
		com_err("failed to receive client cmd");
		client_agent_destroy(agent);
		return -1;
	}

	tlv = (struct clv_tlv *)(agent->ipc_rx_buf + sizeof(u32));
	length = tlv->length;
	rx_p = agent->ipc_rx_buf + sizeof(u32) + sizeof(*tlv);
	flag = *((u32 *)(agent->ipc_rx_buf));
	ret = clv_recv(fd, rx_p, length);
	if (ret == -1) {
		com_err("client exit.");
		client_agent_destroy(agent);
		return -1;
	} else if (ret < 0) {
		com_err("failed to receive client cmd");
		client_agent_destroy(agent);
		return -1;
	}

	if (tlv->tag != CLV_TAG_WIN) {
		com_err("invalid TAG, not a win. 0x%08X", tlv->tag);
		return -1;
	}

	if (flag & (1 << CLV_CMD_CREATE_SURFACE_SHIFT)) {
		ret = clv_server_parse_create_surface_cmd(agent->ipc_rx_buf,
							  &si);
		if (ret < 0) {
			com_err("failed to parse surface create command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			com_debug("parse surface create command ok.");
			com_debug("Surf: %d, %d,%d:%ux%u, %ux%u, %d,%d:%ux%u",
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
			com_debug("Surf created 0x%08lX", id);
		}
		clv_dup_surface_id_cmd(agent->surface_id_created_tx_cmd,
				       agent->surface_id_created_tx_cmd_t,
				       agent->surface_id_created_tx_len, id);
		ret = clv_send(fd, agent->surface_id_created_tx_cmd,
			       agent->surface_id_created_tx_len);
		if (ret == -1) {
			com_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			com_err("failed to send surface id");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_CREATE_VIEW_SHIFT)) {
		ret = clv_server_parse_create_view_cmd(agent->ipc_rx_buf,
						       &vi);
		if (ret < 0) {
			com_err("failed to parse view create command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			com_debug("parse view create command ok.");
			com_debug("View: %u, %d,%d:%ux%u, %f, 0x%08X, %u",
				  vi.type, vi.area.pos.x, vi.area.pos.y,
				  vi.area.w, vi.area.h,
				  vi.alpha, vi.output_mask,
				  vi.primary_output);
			agent->view = clv_view_create(agent->surface, &vi);
			assert(agent->view);
			id = (u64)(agent->view);
			com_debug("View created 0x%08lX", id);
		}
		clv_dup_view_id_cmd(agent->view_id_created_tx_cmd,
				    agent->view_id_created_tx_cmd_t,
				    agent->view_id_created_tx_len, id);
		ret = clv_send(fd, agent->view_id_created_tx_cmd,
			       agent->view_id_created_tx_len);
		if (ret == -1) {
			com_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			com_err("failed to send view id");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_CREATE_BO_SHIFT)) {
		ret = clv_server_parse_create_bo_cmd(agent->ipc_rx_buf, &bi);
		if (ret < 0) {
			com_err("failed to parse bo create command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			com_debug("parse bo create command ok.");
			if (bi.type == CLV_BUF_TYPE_SHM) {
				com_debug("SHM BO create req: %u, %s, %u:%ux%u "
					  "%lu", bi.fmt, bi.name, bi.width,
					  bi.stride, bi.height, bi.surface_id);
				buf = shm_buffer_create(&bi);
				assert(buf);
				list_add_tail(&buf->link, &agent->buffers);
				id = (u64)buf;
				com_debug("SHM-BUF BO created 0x%08lX", id);
			} else if (bi.type == CLV_BUF_TYPE_DMA) {
				com_debug("DMA-BUF BO create req: %u, %u, "
					  "%u:%ux%u %lu", bi.fmt,
					  bi.internal_fmt, bi.width, bi.stride,
					  bi.height, bi.surface_id);
				dmabuf_fd = clv_recv_fd(fd);
				com_debug("receive dma buf fd %d", dmabuf_fd);
				if (dmabuf_fd < 0) {
					com_err("dmabuf illegal %d", dmabuf_fd);
					client_agent_destroy(agent);
					return -1;
				}
				if (agent->view->type == CLV_VIEW_TYPE_OVERLAY){
					buf = calloc(1, sizeof(*buf));
					buf->type = CLV_BUF_TYPE_DMA;
					buf->w = bi.width;
					buf->h = bi.height;
					buf->fd = dmabuf_fd;
					buf->stride = bi.stride;
					if (bi.vstride)
						buf->vstride = bi.vstride;
					else
						buf->vstride = 0;
					buf->internal_fb = NULL;
					if (bi.fmt == CLV_PIXEL_FMT_NV12) {
//						clv_debug("receive fd %d",
//							buf->fd);
						buf->size = bi.stride*bi.height
								* 3 / 2;
						buf->pixel_fmt
							= CLV_PIXEL_FMT_NV12;
					} else if (bi.fmt==CLV_PIXEL_FMT_NV24) {
						buf->size = bi.stride*bi.height
								* 3;
						buf->pixel_fmt
							= CLV_PIXEL_FMT_NV24;
					}
				} else {
					buf = agent->c->renderer->import_dmabuf(
						agent->c, dmabuf_fd,
						bi.width, bi.height,
						bi.stride, bi.vstride, bi.fmt,
						bi.internal_fmt);
				}
				assert(buf);
				list_add_tail(&buf->link, &agent->buffers);
				id = (u64)buf;
				com_debug("DMA-BUF BO created 0x%08lX", id);
			}
		}
		clv_dup_bo_id_cmd(agent->bo_id_created_tx_cmd,
				  agent->bo_id_created_tx_cmd_t,
				  agent->bo_id_created_tx_len, id);
		ret = clv_send(fd, agent->bo_id_created_tx_cmd,
			       agent->bo_id_created_tx_len);
		if (ret == -1) {
			com_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			com_err("failed to send bo id");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_DESTROY_BO_SHIFT)) {
		id = clv_server_parse_destroy_bo_cmd(agent->ipc_rx_buf);
		if (!id) {
			com_err("failed to parse destroy bo command from "
				"agent 0x%08lX", (u64)agent);
		} else {
			buf = (struct clv_buffer *)id;
			com_debug("parse destroy bo command ok. bo_id = %lu",
				  id);
//			clv_debug("parse destroy bo command ok. bo_id = %lu",
//				  id);
			client_destroy_buf(agent, buf);
		}
	} else if (flag & (1 << CLV_CMD_COMMIT_SHIFT)) {
		//clock_gettime(CLOCK_MONOTONIC, &ts1);
		//clv_debug("r: %3d.%06d", ts1.tv_sec, ts1.tv_nsec/1000000l);
		ret = clv_server_parse_commit_req_cmd(agent->ipc_rx_buf, &ci);
		if (ret < 0) {
			com_err("failed to parse commit command from "
				"agent 0x%08lX", (u64)agent);
			id = 0;
		} else {
			buf = (struct clv_buffer *)(ci.bo_id);
			com_debug("parse commit command ok. bo_id = %lu",
				  ci.bo_id);
			if (!ci.bo_id) {
				com_err("commit bo_id == 0!!!");
				goto ack_commit;
			}
			agent->view->area.pos.x = ci.view_x;
			agent->view->area.pos.y = ci.view_y;
			com_debug("set view %p pos: %d, %d", agent->view,
				  agent->view->area.pos.x,
				  agent->view->area.pos.y);
			agent->view->hot_x = ci.view_hot_x;
			agent->view->hot_y = ci.view_hot_y;
			if (buf->type == CLV_BUF_TYPE_SHM) {
				com_debug("Commit info: 0x%08lX, %d, %d,"
					  "%d:%ux%u %d damage:%d,%d %ux%u",
					  ci.bo_id, ci.shown,
					  ci.view_x, ci.view_y,
					  ci.view_width, ci.view_height,
					  ci.delta_z,
					  ci.bo_damage.pos.x,ci.bo_damage.pos.y,
					  ci.bo_damage.w, ci.bo_damage.h);
				if (agent->view->type == CLV_VIEW_TYPE_CURSOR) {
					com_debug("receive cursor bo %lu's cmt",
						  ci.bo_id);
					if (agent->view->cursor_buf != buf) {
						clv_debug("cursor bo changed.");
					}
					agent->view->cursor_buf = buf;
				}
				clv_region_fini(&agent->surface->damage);
				if (ci.bo_damage.w && ci.bo_damage.h)
					clv_region_init_rect(
						&agent->surface->damage,
						ci.bo_damage.pos.x,
						ci.bo_damage.pos.y,
						ci.bo_damage.w,
						ci.bo_damage.h);
				else
					clv_region_init(
						&agent->surface->damage);
				if (agent->view->type == CLV_VIEW_TYPE_PRIMARY){
					agent->c->renderer->attach_buffer(
						agent->surface, buf);
					if (ci.bo_damage.w && ci.bo_damage.h) {
						clock_gettime(
							agent->c->clk_id, &t1);
						agent->c->renderer-> \
						    flush_damage(
							agent->surface);
						clock_gettime(agent->c->clk_id,
								&t2);
						com_debug("[TIMER] flush spent "
							"%ld ms",
							timespec_sub_to_msec(
								&t2, &t1));
					}
				}
				clv_region_fini(&agent->surface->damage);
			} else if (buf->type == CLV_BUF_TYPE_DMA) {
				com_debug("Commit info: 0x%08lX, %d, %d,"
					  "%d:%ux%u %d",
					  ci.bo_id, ci.shown,
					  ci.view_x, ci.view_y,
					  ci.view_width, ci.view_height,
					  ci.delta_z);

				if (agent->view->type == CLV_VIEW_TYPE_PRIMARY){
					agent->c->renderer->attach_buffer(
						agent->surface, buf);
				} else {
					com_debug("attach dma buf %p", buf);
					agent->view->last_dmafb = 
						agent->view->curr_dmafb;
					//clock_gettime(CLOCK_MONOTONIC, &ts1);
					//clv_debug("rr: %3d.%06d", ts1.tv_sec, ts1.tv_nsec/1000000l);
					agent->view->curr_dmafb = 
					    agent->c->backend->import_dmabuf(
					        agent->c, buf);
					com_debug("view %p's curr_dmafb = %p",
					    agent->view,
					    agent->view->curr_dmafb);
				}
			}

			agent->view->plane = &agent->c->primary_plane;
			clv_view_schedule_repaint(agent->view);
			if (agent->view->type != CLV_VIEW_TYPE_CURSOR) {
				com_debug("************ add flip listener");
				clv_surface_add_flip_listener(agent->surface);
			} else {
				if (ci.bo_damage.w && ci.bo_damage.h) {
					com_debug("****** add flip listener");
					clv_surface_add_flip_listener(
						agent->surface);
				}
			}
			id = 1;
		}
ack_commit:
		clv_dup_commit_ack_cmd(agent->commit_ack_tx_cmd,
				    agent->commit_ack_tx_cmd_t,
				    agent->commit_ack_tx_len, id);
		ret = clv_send(fd, agent->commit_ack_tx_cmd,
			       agent->commit_ack_tx_len);
		if (ret == -1) {
			com_err("client exit.");
			client_agent_destroy(agent);
			return -1;
		} else if (ret < 0) {
			com_err("failed to send commit ack");
			client_agent_destroy(agent);
			return -1;
		}
	} else if (flag & (1 << CLV_CMD_SHELL_SHIFT)) {
		ret = clv_parse_shell_cmd(agent->ipc_rx_buf, &shell);
		if (ret < 0) {
			com_err("failed to parse shell command from "
				"agent 0x%08lX", (u64)agent);
		} else {
			com_debug("parse shell command ok.");
			if (shell.cmd == CLV_SHELL_DEBUG_SETTING) {
				clv_debug("Debug Setting.");
				f = 0;
				clv_debug("COMMON: %u",
				    shell.value.dbg_flags.common_flag);
				f |= shell.value.dbg_flags.common_flag;
				f &= 0x0F;
				set_common_dbg(f);
				f1 = 0;
				clv_debug("Compositor: %u",
				    shell.value.dbg_flags.compositor_flag);
				f1 |= shell.value.dbg_flags.compositor_flag;
				f1 &= 0x0F;
				f = 0;
				clv_debug("DRM: %u",
				    shell.value.dbg_flags.drm_flag);
				f |= shell.value.dbg_flags.drm_flag;
				f &= 0x0F;
				clv_debug("GBM: %u",
				    shell.value.dbg_flags.gbm_flag);
				f |= (shell.value.dbg_flags.gbm_flag << 4);
				f &= 0x0FF;
				clv_debug("PS: %u",
				    shell.value.dbg_flags.ps_flag);
				f |= (shell.value.dbg_flags.ps_flag << 8);
				f &= 0x0FFF;
				clv_debug("TS: %u",
				    shell.value.dbg_flags.timer_flag);
				f |= (shell.value.dbg_flags.timer_flag << 12);
				f1 |= (shell.value.dbg_flags.timer_flag << 4);
				f1 &= 0x0FF;
				set_compositor_dbg(f1);
				f &= 0x0FFFF;
				set_scanout_dbg(f);
				f = 0;
				clv_debug("GLES: %u",
				    shell.value.dbg_flags.gles_flag);
				f |= shell.value.dbg_flags.gles_flag;
				f &= 0x0F;
				clv_debug("EGL: %u",
				    shell.value.dbg_flags.egl_flag);
				f |= (shell.value.dbg_flags.egl_flag << 4);
				f &= 0x0FF;
				set_renderer_dbg(f);
			} else if (shell.cmd == CLV_SHELL_CANVAS_LAYOUT_QUERY) {
				com_debug("request canvas layout.");
				config = server.config;
				shell.value.layout.mode = (u32)(config->mode);
				shell.value.layout.count_heads =
					config->count_heads;
				for (i = 0; i < config->count_heads; i++) {
					shell.value.layout.desktops[i].pos.x =
					    config->heads[i].encoder \
					        .output.render_area.pos.x;
					shell.value.layout.desktops[i].pos.y =
					    config->heads[i].encoder \
					        .output.render_area.pos.y;
					shell.value.layout.desktops[i].w =
					    config->heads[i].encoder \
					        .output.render_area.w;
					shell.value.layout.desktops[i].h =
					    config->heads[i].encoder \
					        .output.render_area.h;
				}
				shell_tx_buf = clv_create_shell_cmd(&shell, &n);
				if (shell_tx_buf)
					clv_send(fd, shell_tx_buf, n);
			} else if (shell.cmd==CLV_SHELL_CANVAS_LAYOUT_SETTING) {
				struct clv_client_agent *agt;
				com_debug("receive layout setting command.");
				update_layout(agent->c, &shell);

				shell_tx_buf = clv_create_shell_cmd(&shell, &n);
				/*
				 * send layout change event to each agent.
				 * if (shell_tx_buf)
				 * 	clv_send(fd, shell_tx_buf, n);
				 */
				list_for_each_entry(agt, &server.client_agents,
							link) {
					if (shell_tx_buf) {
						clv_send(agt->sock,
							shell_tx_buf, n);
					}
				}
			}
		}
	} else {
		com_err("unknown command 0x%08X", flag);
		return -1;
	}

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
	com_info("Send link id 0x%08lX", (u64)agent);
	assert(clv_send(sock, s->linkid_created_ack_tx_cmd,
			s->linkid_created_ack_tx_len) == 0);
	com_info("a new client connected. sock = %d", sock);
	
	return 0;
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	struct clv_client_agent *agent, *next;
	u32 n;
	cpu_set_t set;

	server.linkid_created_ack_tx_cmd_t
		= clv_server_create_linkup_cmd(0, &n);
	assert(server.linkid_created_ack_tx_cmd_t);
	server.linkid_created_ack_tx_cmd = malloc(n);
	assert(server.linkid_created_ack_tx_cmd);
	server.linkid_created_ack_tx_len = n;
	
	server.hpd_listener.notify = head_state_changed_cb;
	timing_choose_mode = USE_PREFERRED;
	while ((ch = getopt_long(argc, argv, server_short_options,
				 server_options, NULL)) != -1) {
		switch (ch) {
		case 'h':
			timing_choose_mode = USE_HIGHVFREQ;
			break;
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

	com_info("Run as daemon: %s", run_as_daemon ? "Y" : "N");
	com_info("DRM device node: %s", drm_node);
	com_info("Config file: %s", config_xml);

	if (run_as_daemon)
		run_daemon();

	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(5, sizeof(set), &set);
	server.config = load_config_from_file(config_xml);
	server.count_outputs = server.config->count_heads;

	server.display = clv_display_create();
	if (!server.display) {
		com_err("cannot create clover display.");
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

