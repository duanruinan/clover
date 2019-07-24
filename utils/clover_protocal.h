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

#ifndef CLOVER_PROTOCAL_H
#define CLOVER_PROTOCAL_H

#include <clover_utils.h>

#define CLV_CMD_OFFSET 0

enum clv_cmd_shift {
	/* server send connection id to client */
	CLV_CMD_LINK_ID_ACK_SHIFT = CLV_CMD_OFFSET,
	/* client sends a surface's creation request */
	CLV_CMD_CREATE_SURFACE_SHIFT,
	/* server feeds back the result */
	CLV_CMD_CREATE_SURFACE_ACK_SHIFT,
	/* client sends a view's creation request */
	CLV_CMD_CREATE_VIEW_SHIFT,
	/* server feeds back the result */
	CLV_CMD_CREATE_VIEW_ACK_SHIFT,

	/*
	 * client sends a share memory / DMA-BUF buffer object creation request
	 */
	CLV_CMD_CREATE_BO_SHIFT,
	/* server feeds back the result of BO creation */
	CLV_CMD_CREATE_BO_ACK_SHIFT,

	/*
	 * Client commit the new state of surface to server.
	 * Commit: Changes of buffer object/view/surface
	 *
	 * Client should not render into the BO which has been commited to
	 * server, until received CLV_CMD_BO_COMPLETE from server.
	 *
	 * Once a new BO is commited to server, client should switch the working
	 * BO to another (double buffer) which has not been attached to server.
	 *
	 * Notice: Client should invoke glFinish before commit
	 *         to ensure the completion of the previouse rendering work.
	 *
	 *         Server should schedule a new repaint request.
	 */
	CLV_CMD_COMMIT_SHIFT,
	/* server feeds back the result of BO's attaching operation */
	CLV_CMD_COMMIT_ACK_SHIFT,
	/* server notify client the BO is no longer in use */
	CLV_CMD_BO_COMPLETE_SHIFT,
	/* mouse & kbd event report */
	CLV_CMD_INPUT_EVT_SHIFT,

	/* <----------------- DESTROYING STAGE ------------------> */
	/*
	 * Client requests to destroy surface
	 * Server will destroy all resources of the given surface.
	 */
	CLV_CMD_DESTROY_SHIFT,
	/* server feeds back the result of the surface's destruction */
	CLV_CMD_DESTROY_ACK_SHIFT,

	/* <---------------- Clover setting utils ---------------> */
	CLV_CMD_SHELL_SHIFT,
	CLV_CMD_LAST_SHIFT,
};

enum clv_tag {
	CLV_TAG_WIN = 0,
	CLV_TAG_INPUT,
	CLV_TAG_MAP, /* array of u32 */
	CLV_TAG_RESULT, /* for all ACK CMDs. u64 */
	CLV_TAG_CREATE_SURFACE, /* clv_surface_info */
	CLV_TAG_CREATE_VIEW, /* clv_view_info */
	CLV_TAG_CREATE_BO, /* clv_buf_info */
	CLV_TAG_COMMIT_INFO, /* clv_commit_info */
	CLV_TAG_SHELL, /* clv_shell_info */
	CLV_TAG_DESTROY,
};

struct clv_tlv {
	enum clv_tag tag;
	u32 length; /* payload's size */
	u8 payload[0];
};

/*
 * cmd head: flag:4 bytes                       MUST  (Offs 0)
 * cmd head: TAG: 4 bytes WIN/INPUT TAG         MUST  (Offs 4)
 * total length: 4 bytes                        MUST  (Offs 8)
 * payload {                                    MUST
 *     TAG: 4 bytes TAG_MAP                     MUST
 *     length: 4 bytes ---- N                   MUST
 *     map {                                    MUST
 *         offset[0],       offset of TLV(CLV_CMD_XXX_SHIFT - CLV_CMD_OFFSET)
 *         offset[1], ...
 *         offset[n-1],
 *     };
 *     TAG[0]: 4 bytes TAG_XXX                  OPTS  (Offs offset[0])
 *     length[0]: 4 bytes
 *     payload[0]: length(length[0])
 *     TAG[1]: 4 bytes TAG_XXX                  OPTS  (Offs offset[1])
 *     length[1]: 4 bytes
 *     payload[1]: length(length[1])
 *     ...
 *     TAG[n-1]: 4 bytes TAG_XXX                OPTS  (Offs offset[n-1])
 *     length[n-1]: 4 bytes
 *     payload[n-1]: length(length[n-1])
 * };
 */

#define CLV_CMD_MAP_SIZE (sizeof(struct clv_tlv) \
			+ (CLV_CMD_LAST_SHIFT - CLV_CMD_OFFSET) * sizeof(u32))

struct clv_surface_info {
	u64 surface_id;
	s32 is_opaque;
	struct clv_rect damage;
	u32 width, height;
	struct clv_rect opaque;
};

enum clv_view_type {
	CLV_VIEW_TYPE_PRIMARY = 0,
	CLV_VIEW_TYPE_OVERLAY,
	CLV_VIEW_TYPE_CURSOR,
};

struct clv_view_info {
	u64 view_id;
	enum clv_view_type type;
	struct clv_rect area;
	float alpha;
	u32 output_mask;
	u32 primary_output;
};

enum clv_pixel_fmt {
	CLV_PIXEL_FMT_UNKNOWN = 0,
	CLV_PIXEL_FMT_XRGB8888, /* SHM / DMA-BUF */
	CLV_PIXEL_FMT_ARGB8888, /* SHM / DMA-BUF */
	CLV_PIXEL_FMT_YUV420P, /* SHM */
	CLV_PIXEL_FMT_YUV444P, /* SHM */
	CLV_PIXEL_FMT_NV12, /* DMA-BUF (no render) only */
	CLV_PIXEL_FMT_NV24, /* DMA-BUF (no render) only */
};

#define CLV_BUFFER_NAME_LEN 32

enum clv_buffer_type {
	CLV_BUF_TYPE_UNKNOWN = 0,
	CLV_BUF_TYPE_SHM,
	CLV_BUF_TYPE_DMA,
};

struct clv_bo_info {
	enum clv_buffer_type type;
	enum clv_pixel_fmt fmt;
	u32 internal_fmt;
	u32 count_planes;
	char name[CLV_BUFFER_NAME_LEN];
	u32 width, stride, height;
	u64 surface_id;
};

struct clv_commit_info {
	u64 bo_id;
	struct clv_rect bo_damage;

	s32 shown; /* 0: hide / 1: show */

	s32 view_x, view_y;
	u32 view_width, view_height;

	/* 0: z order no change / 1: bring to top / -1: falling down */
	s32 delta_z;
};

enum clv_shell_cmd {
	CLV_SHELL_DEBUG_SETTING,
	CLV_SHELL_CANVAS_LAYOUT_SETTING,
	CLV_SHELL_CANVAS_LAYOUT_QUERY,
};

struct clv_canvas_layout {
	u32 count_heads;
	u32 mode;
	struct clv_rect desktops[8];
};

struct clv_debug_flags {
	u8 common_flag;
	u8 compositor_flag;
	u8 drm_flag;
	u8 gbm_flag;
	u8 ps_flag;
	u8 timer_flag;
	u8 gles_flag;
	u8 egl_flag;
};

struct clv_shell_info {
	enum clv_shell_cmd cmd;
	union {
		struct clv_debug_flags dbg_flags;
		struct clv_canvas_layout layout;
	} value;
};

struct clv_input_event {
	uint16_t type;
	uint16_t code;
	union {
		uint32_t value;
		struct {
			uint16_t x;
			uint16_t y;
			int16_t dx;
			int16_t dy;
		} pos;
	} v;
};

u8 *clv_server_create_linkup_cmd(u64 link_id, u32 *n);
u8 *clv_dup_linkup_cmd(u8 *dst, u8 *src, u32 n, u64 link_id);
u64 clv_client_parse_link_id(u8 *data);
u8 *clv_client_create_surface_cmd(struct clv_surface_info *s, u32 *n);
u8 *clv_dup_create_surface_cmd(u8 *dst, u8 *src, u32 n,
			       struct clv_surface_info *s);
s32 clv_server_parse_create_surface_cmd(u8 *data, struct clv_surface_info *s);
u8 *clv_server_create_surface_id_cmd(u64 surface_id, u32 *n);
u8 *clv_dup_surface_id_cmd(u8 *dst, u8 *src, u32 n, u64 surface_id);
u64 clv_client_parse_surface_id(u8 *data);
u8 *clv_client_create_view_cmd(struct clv_view_info *v, u32 *n);
u8 *clv_dup_create_view_cmd(u8 *dst, u8 *src, u32 n, struct clv_view_info *v);
s32 clv_server_parse_create_view_cmd(u8 *data, struct clv_view_info *v);
u8 *clv_server_create_view_id_cmd(u64 view_id, u32 *n);
u8 *clv_dup_view_id_cmd(u8 *dst, u8 *src, u32 n, u64 view_id);
u64 clv_client_parse_view_id(u8 *data);
u8 *clv_client_create_bo_cmd(struct clv_bo_info *b, u32 *n);
u8 *clv_dup_create_bo_cmd(u8 *dst, u8 *src, u32 n, struct clv_bo_info *b);
s32 clv_server_parse_create_bo_cmd(u8 *data, struct clv_bo_info *b);
u8 *clv_server_create_bo_id_cmd(u64 bo_id, u32 *n);
u8 *clv_dup_bo_id_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id);
u64 clv_client_parse_bo_id(u8 *data);
u8 *clv_client_create_commit_req_cmd(struct clv_commit_info *c, u32 *n);
u8 *clv_dup_commit_req_cmd(u8 *dst, u8 *src, u32 n, struct clv_commit_info *c);
s32 clv_server_parse_commit_req_cmd(u8 *data, struct clv_commit_info *c);
u8 *clv_server_create_commit_ack_cmd(u64 ret, u32 *n);
u8 *clv_dup_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 clv_client_parse_commit_ack_cmd(u8 *data);
u8 *clv_server_create_bo_complete_cmd(u64 ret, u32 *n);
u8 *clv_dup_bo_complete_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 clv_client_parse_bo_complete_cmd(u8 *data);
u8 *clv_create_shell_cmd(struct clv_shell_info *s, u32 *n);
u8 *clv_dup_shell_cmd(u8 *dst, u8 *src, u32 n, struct clv_shell_info *s);
s32 clv_parse_shell_cmd(u8 *data, struct clv_shell_info *s);
u8 *clv_client_create_destroy_cmd(u64 link_id, u32 *n);
u8 *clv_dup_destroy_cmd(u8 *dst, u8 *src, u32 n, u64 link_id);
u64 clv_server_parse_destroy_cmd(u8 *data);
u8 *clv_server_create_destroy_ack_cmd(u64 ret, u32 *n);
u8 *clv_dup_destroy_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 clv_client_parse_destroy_ack_cmd(u8 *data);
u8 *clv_server_create_input_evt_cmd(struct clv_input_event *evts,
				    u32 count_evts, u32 *n);
u8 *clv_server_fill_input_evt_cmd(u8 *dst, struct clv_input_event *evts,
				  u32 count_evts, u32 *n, u32 max_size);
void clv_cmd_dump(u8 *data);

#endif

