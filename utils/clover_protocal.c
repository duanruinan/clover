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

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_protocal.h>

u8 *clv_server_create_linkup_cmd(u64 link_id, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_LINK_ID_ACK_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_result = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_LINK_ID_ACK_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CLV_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = link_id;
	*n = size;

	return p;
}

u8 *clv_dup_linkup_cmd(u8 *dst, u8 *src, u32 n, u64 link_id)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_link_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_link_id = (struct clv_tlv *)(dst
			+ map[CLV_CMD_LINK_ID_ACK_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_link_id->payload[0])) = link_id;
	return dst;
}

u64 clv_client_parse_link_id(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_LINK_ID_ACK_SHIFT))) {
		clv_err("not link id cmd");
		return 0;
	}

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_LINK_ID_ACK_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_LINK_ID_ACK_SHIFT - CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *clv_client_create_surface_cmd(struct clv_surface_info *s, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_surface_create;
	u32 size, size_surface_create, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_surface_create = sizeof(*tlv) + sizeof(*s);
	size = sizeof(*tlv) + size_map + size_surface_create + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_CREATE_SURFACE_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_surface_create + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_surface_create = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_CREATE_SURFACE_SHIFT - CLV_CMD_OFFSET]
		= (u8 *)tlv_surface_create - p;
	tlv_surface_create->tag = CLV_TAG_CREATE_SURFACE;
	tlv_surface_create->length = sizeof(*s);
	memcpy(&tlv_surface_create->payload[0], s, sizeof(*s));
	*n = size;

	return p;
}

u8 *clv_dup_create_surface_cmd(u8 *dst, u8 *src, u32 n,
			       struct clv_surface_info *s)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_surface_create;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_surface_create = (struct clv_tlv *)(dst
			+ map[CLV_CMD_CREATE_SURFACE_SHIFT-CLV_CMD_OFFSET]);
	memcpy(&tlv_surface_create->payload[0], s, sizeof(*s));
	return dst;
}

s32 clv_server_parse_create_surface_cmd(u8 *data, struct clv_surface_info *s)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_surface_create;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_CREATE_SURFACE_SHIFT)))
		return -1;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_CREATE_SURFACE_SHIFT - CLV_CMD_OFFSET] >= size)
		return -1;
	tlv_surface_create = (struct clv_tlv *)(data
			+ map[CLV_CMD_CREATE_SURFACE_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_surface_create->tag != CLV_TAG_CREATE_SURFACE)
		return -1;
	if (tlv_surface_create->length != sizeof(*s))
		return -1;
	memcpy(s, &tlv_surface_create->payload[0], sizeof(*s));
	return 0;
}

u8 *clv_server_create_surface_id_cmd(u64 surface_id, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_surface_id;
	u32 size, size_surface_id, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_surface_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_surface_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_CREATE_SURFACE_ACK_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_surface_id + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_surface_id = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_CREATE_SURFACE_ACK_SHIFT - CLV_CMD_OFFSET]
		= (u8 *)tlv_surface_id - p;
	tlv_surface_id->tag = CLV_TAG_RESULT;
	tlv_surface_id->length = sizeof(u64);
	*((u64 *)(&tlv_surface_id->payload[0])) = surface_id;
	*n = size;

	return p;
}

u8 *clv_dup_surface_id_cmd(u8 *dst, u8 *src, u32 n, u64 surface_id)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_surface_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_surface_id = (struct clv_tlv *)(dst
			+ map[CLV_CMD_CREATE_SURFACE_ACK_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_surface_id->payload[0])) = surface_id;
	return dst;
}

u64 clv_client_parse_surface_id(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_CREATE_SURFACE_ACK_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_CREATE_SURFACE_ACK_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_CREATE_SURFACE_ACK_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *clv_client_create_view_cmd(struct clv_view_info *v, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_view_create;
	u32 size, size_view_create, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_view_create = sizeof(*tlv) + sizeof(*v);
	size = sizeof(*tlv) + size_map + size_view_create + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_CREATE_VIEW_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_view_create + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_view_create = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_CREATE_VIEW_SHIFT - CLV_CMD_OFFSET]
		= (u8 *)tlv_view_create - p;
	tlv_view_create->tag = CLV_TAG_CREATE_VIEW;
	tlv_view_create->length = sizeof(*v);
	memcpy(&tlv_view_create->payload[0], v, sizeof(*v));
	*n = size;

	return p;
}

u8 *clv_dup_create_view_cmd(u8 *dst, u8 *src, u32 n, struct clv_view_info *v)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_view_create;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_view_create = (struct clv_tlv *)(dst
			+ map[CLV_CMD_CREATE_VIEW_SHIFT-CLV_CMD_OFFSET]);
	memcpy(&tlv_view_create->payload[0], v, sizeof(*v));
	return dst;
}

s32 clv_server_parse_create_view_cmd(u8 *data, struct clv_view_info *v)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_view_create;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_CREATE_VIEW_SHIFT)))
		return -1;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_CREATE_VIEW_SHIFT - CLV_CMD_OFFSET] >= size)
		return -1;
	tlv_view_create = (struct clv_tlv *)(data
			+ map[CLV_CMD_CREATE_VIEW_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_view_create->tag != CLV_TAG_CREATE_VIEW)
		return -1;
	if (tlv_view_create->length != sizeof(*v))
		return -1;
	memcpy(v, &tlv_view_create->payload[0], sizeof(*v));
	return 0;
}

u8 *clv_server_create_view_id_cmd(u64 view_id, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_view_id;
	u32 size, size_view_id, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_view_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_view_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_CREATE_VIEW_ACK_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_view_id + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_view_id = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_CREATE_VIEW_ACK_SHIFT - CLV_CMD_OFFSET]
		= (u8 *)tlv_view_id - p;
	tlv_view_id->tag = CLV_TAG_RESULT;
	tlv_view_id->length = sizeof(u64);
	*((u64 *)(&tlv_view_id->payload[0])) = view_id;
	*n = size;

	return p;
}

u8 *clv_dup_view_id_cmd(u8 *dst, u8 *src, u32 n, u64 view_id)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_view_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_view_id = (struct clv_tlv *)(dst
			+ map[CLV_CMD_CREATE_VIEW_ACK_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_view_id->payload[0])) = view_id;
	return dst;
}

u64 clv_client_parse_view_id(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_CREATE_VIEW_ACK_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_CREATE_VIEW_ACK_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_CREATE_VIEW_ACK_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *clv_client_create_bo_cmd(struct clv_bo_info *b, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_create;
	u32 size, size_bo_create, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_bo_create = sizeof(*tlv) + sizeof(*b);
	size = sizeof(*tlv) + size_map + size_bo_create + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CLV_CMD_CREATE_BO_SHIFT;

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_bo_create + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_bo_create = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_CREATE_BO_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_bo_create - p;
	tlv_bo_create->tag = CLV_TAG_CREATE_BO;
	tlv_bo_create->length = sizeof(*b);
	memcpy(&tlv_bo_create->payload[0], b, sizeof(*b));
	*n = size;

	return p;
}

u8 *clv_dup_create_bo_cmd(u8 *dst, u8 *src, u32 n, struct clv_bo_info *b)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_create;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_bo_create = (struct clv_tlv *)(dst
			+ map[CLV_CMD_CREATE_BO_SHIFT-CLV_CMD_OFFSET]);
	memcpy(&tlv_bo_create->payload[0], b, sizeof(*b));
	return dst;
}

s32 clv_server_parse_create_bo_cmd(u8 *data, struct clv_bo_info *b)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_create;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_CREATE_BO_SHIFT)))
		return -1;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_CREATE_BO_SHIFT - CLV_CMD_OFFSET] >= size)
		return -1;
	tlv_bo_create = (struct clv_tlv *)(data
			+ map[CLV_CMD_CREATE_BO_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_bo_create->tag != CLV_TAG_CREATE_BO)
		return -1;
	if (tlv_bo_create->length != sizeof(*b))
		return -1;
	memcpy(b, &tlv_bo_create->payload[0], sizeof(*b));
	return 0;
}

u8 *clv_server_create_bo_id_cmd(u64 bo_id, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 size, size_bo_id, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_bo_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_bo_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CLV_CMD_CREATE_BO_ACK_SHIFT;

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_bo_id + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_bo_id = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_CREATE_BO_ACK_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_bo_id - p;
	tlv_bo_id->tag = CLV_TAG_RESULT;
	tlv_bo_id->length = sizeof(u64);
	*((u64 *)(&tlv_bo_id->payload[0])) = bo_id;
	*n = size;

	return p;
}

u8 *clv_dup_bo_id_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_bo_id = (struct clv_tlv *)(dst
			+ map[CLV_CMD_CREATE_BO_ACK_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_bo_id->payload[0])) = bo_id;
	return dst;
}

u64 clv_client_parse_bo_id(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_CREATE_BO_ACK_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_CREATE_BO_ACK_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_CREATE_BO_ACK_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

/********************************************************/
u8 *clv_client_destroy_bo_cmd(u64 bo_id, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 size, size_bo_id, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_bo_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_bo_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_DESTROY_BO_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_bo_id + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_bo_id = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_DESTROY_BO_SHIFT - CLV_CMD_OFFSET]
		= (u8 *)tlv_bo_id - p;
	tlv_bo_id->tag = CLV_TAG_RESULT;
	tlv_bo_id->length = sizeof(u64);
	*((u64 *)(&tlv_bo_id->payload[0])) = bo_id;
	*n = size;

	return p;
}

u8 *clv_dup_destroy_bo_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_bo_id = (struct clv_tlv *)(dst
			+ map[CLV_CMD_DESTROY_BO_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_bo_id->payload[0])) = bo_id;
	return dst;
}

u64 clv_server_parse_destroy_bo_cmd(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_DESTROY_BO_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_DESTROY_BO_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_DESTROY_BO_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

/********************************************************/

u8 *clv_client_create_commit_req_cmd(struct clv_commit_info *c, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_commit;
	u32 size, size_commit, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_commit = sizeof(*tlv) + sizeof(*c);
	size = sizeof(*tlv) + size_map + size_commit + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_COMMIT_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_commit + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_commit = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_COMMIT_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_commit - p;
	tlv_commit->tag = CLV_TAG_COMMIT_INFO;
	tlv_commit->length = sizeof(*c);
	memcpy(&tlv_commit->payload[0], c, sizeof(*c));
	*n = size;

	return p;
}

u8 *clv_dup_commit_req_cmd(u8 *dst, u8 *src, u32 n, struct clv_commit_info *c)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_commit;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_commit = (struct clv_tlv *)(dst
			+ map[CLV_CMD_COMMIT_SHIFT-CLV_CMD_OFFSET]);
	memcpy(&tlv_commit->payload[0], c, sizeof(*c));
	return dst;
}

s32 clv_server_parse_commit_req_cmd(u8 *data, struct clv_commit_info *c)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_commit;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_COMMIT_SHIFT)))
		return -1;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_COMMIT_SHIFT - CLV_CMD_OFFSET] >= size)
		return -1;
	tlv_commit = (struct clv_tlv *)(data
			+ map[CLV_CMD_COMMIT_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_commit->tag != CLV_TAG_COMMIT_INFO)
		return -1;
	if (tlv_commit->length != sizeof(*c))
		return -1;
	memcpy(c, &tlv_commit->payload[0], sizeof(*c));
	return 0;
}

u8 *clv_server_create_commit_ack_cmd(u64 ret, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_COMMIT_ACK_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_result = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_COMMIT_ACK_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CLV_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*n = size;

	return p;
}

u8 *clv_dup_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct clv_tlv *)(dst
			+ map[CLV_CMD_COMMIT_ACK_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_result->payload[0])) = ret;
	return dst;
}

u64 clv_client_parse_commit_ack_cmd(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_COMMIT_ACK_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_COMMIT_ACK_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_COMMIT_ACK_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *clv_server_create_bo_complete_cmd(u64 ret, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_BO_COMPLETE_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_result = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_BO_COMPLETE_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CLV_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*n = size;

	return p;
}

u8 *clv_dup_bo_complete_cmd(u8 *dst, u8 *src, u32 n, u64 ret)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct clv_tlv *)(dst
			+ map[CLV_CMD_BO_COMPLETE_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_result->payload[0])) = ret;
	return dst;
}

u64 clv_client_parse_bo_complete_cmd(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_BO_COMPLETE_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_BO_COMPLETE_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_BO_COMPLETE_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *clv_create_shell_cmd(struct clv_shell_info *s, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_shell;
	u32 size, size_shell, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_shell = sizeof(*tlv) + sizeof(*s);
	size = sizeof(*tlv) + size_map + size_shell + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CLV_CMD_SHELL_SHIFT;

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_shell + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_shell = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_SHELL_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_shell - p;
	tlv_shell->tag = CLV_TAG_SHELL;
	tlv_shell->length = sizeof(*s);
	memcpy(&tlv_shell->payload[0], s, sizeof(*s));
	*n = size;

	return p;
}

u8 *clv_dup_shell_cmd(u8 *dst, u8 *src, u32 n, struct clv_shell_info *s)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_shell;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_shell = (struct clv_tlv *)(dst
			+ map[CLV_CMD_SHELL_SHIFT-CLV_CMD_OFFSET]);
	memcpy(&tlv_shell->payload[0], s, sizeof(*s));
	return dst;
}

s32 clv_parse_shell_cmd(u8 *data, struct clv_shell_info *s)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_shell;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_SHELL_SHIFT)))
		return -1;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_SHELL_SHIFT - CLV_CMD_OFFSET] >= size)
		return -1;
	tlv_shell = (struct clv_tlv *)(data
			+ map[CLV_CMD_SHELL_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_shell->tag != CLV_TAG_SHELL)
		return -1;
	if (tlv_shell->length != sizeof(*s))
		return -1;
	memcpy(s, &tlv_shell->payload[0], sizeof(*s));
	return 0;
}

u8 *clv_client_create_destroy_cmd(u64 link_id, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_destroy;
	u32 size, size_destroy, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_destroy = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_destroy + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_DESTROY_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_destroy + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_destroy = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_DESTROY_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_destroy - p;
	tlv_destroy->tag = CLV_TAG_DESTROY;
	tlv_destroy->length = sizeof(u64);
	*((u64 *)(&tlv_destroy->payload[0])) = link_id;
	*n = size;

	return p;
}

u8 *clv_dup_destroy_cmd(u8 *dst, u8 *src, u32 n, u64 link_id)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_destroy;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_destroy = (struct clv_tlv *)(dst
			+ map[CLV_CMD_DESTROY_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_destroy->payload[0])) = link_id;
	return dst;
}

u64 clv_server_parse_destroy_cmd(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_destroy;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_DESTROY_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_DESTROY_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_destroy = (struct clv_tlv *)(data
			+ map[CLV_CMD_DESTROY_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_destroy->tag != CLV_TAG_DESTROY)
		return 0;
	if (tlv_destroy->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_destroy->payload[0]));
}

u8 *clv_server_create_destroy_ack_cmd(u64 ret, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_DESTROY_ACK_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_result = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_DESTROY_ACK_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CLV_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*n = size;

	return p;
}

u8 *clv_dup_destroy_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct clv_tlv *)(dst
			+ map[CLV_CMD_DESTROY_ACK_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_result->payload[0])) = ret;
	return dst;
}

u64 clv_client_parse_destroy_ack_cmd(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_DESTROY_ACK_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_DESTROY_ACK_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_DESTROY_ACK_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *clv_server_create_input_evt_cmd(struct clv_input_event *evts,
				    u32 count_evts, u32 *n)
{
	struct clv_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32)
		+ count_evts * sizeof(struct clv_input_event);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CLV_CMD_INPUT_EVT_SHIFT);

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_INPUT;
	tlv->length = count_evts * sizeof(struct clv_input_event);
	if (evts)
		memcpy(&tlv->payload[0], evts, tlv->length);
	*n = size;

	return p;
}

u8 *clv_server_fill_input_evt_cmd(u8 *dst, struct clv_input_event *evts,
				  u32 count_evts, u32 *n, u32 max_size)
{
	struct clv_tlv *tlv;

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	if ((count_evts * sizeof(struct clv_input_event)) > max_size)
		return NULL;
	if (!evts)
		return NULL;
	tlv->length = count_evts * sizeof(struct clv_input_event);
	memcpy(&tlv->payload[0], evts, tlv->length);
	return dst;
}

struct clv_input_event *clv_client_parse_input_evt_cmd(u8 *data,
						       u32 *count_evts)
{
	struct clv_tlv *tlv;
	u32 *head;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_INPUT_EVT_SHIFT)))
		return NULL;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_INPUT);
	assert(!(tlv->length % sizeof(struct clv_input_event)));
	*count_evts = tlv->length / sizeof(struct clv_input_event);
	return (struct clv_input_event *)(&tlv->payload[0]);
}

void clv_cmd_dump(u8 *data)
{
	struct clv_tlv *tlv;
	u32 head;
	s32 i;

	clv_debug("Dump command");
	head = *((u32 *)data);
	tlv = (struct clv_tlv *)(data + sizeof(u32));
	if (head & (1 << CLV_CMD_LINK_ID_ACK_SHIFT)) {
		clv_debug("LINKID_CMD");
	} else if (head & (1 << CLV_CMD_CREATE_SURFACE_SHIFT)) {
		clv_debug("CREATE_SURFACE_CMD");
	} else if (head & (1 << CLV_CMD_CREATE_SURFACE_ACK_SHIFT)) {
		clv_debug("CREATE_SURFACE_ACK_CMD");
	} else if (head & (1 << CLV_CMD_CREATE_VIEW_SHIFT)) {
		clv_debug("CREATE_VIEW_CMD");
	} else if (head & (1 << CLV_CMD_CREATE_VIEW_ACK_SHIFT)) {
		clv_debug("CREATE_VIEW_ACK_CMD");
	} else if (head & (1 << CLV_CMD_CREATE_BO_SHIFT)) {
		clv_debug("CREATE_BO_CMD");
	} else if (head & (1 << CLV_CMD_CREATE_BO_ACK_SHIFT)) {
		clv_debug("CREATE_BO_ACK_CMD");
	} else if (head & (1 << CLV_CMD_COMMIT_SHIFT)) {
		clv_debug("COMMIT_CMD");
	} else if (head & (1 << CLV_CMD_COMMIT_ACK_SHIFT)) {
		clv_debug("COMMIT_ACK_CMD");
	} else if (head & (1 << CLV_CMD_BO_COMPLETE_SHIFT)) {
		clv_debug("BO_COMPLETE_CMD");
	} else if (head & (1 << CLV_CMD_INPUT_EVT_SHIFT)) {
		clv_debug("INPUT_EVT_CMD");
	} else if (head & (1 << CLV_CMD_DESTROY_SHIFT)) {
		clv_debug("DESTROY_CMD");
	} else if (head & (1 << CLV_CMD_DESTROY_ACK_SHIFT)) {
		clv_debug("DESTROY_ACK_CMD");
	} else if (head & (1 << CLV_CMD_SHELL_SHIFT)) {
		clv_debug("SHELL_CMD");
	} else {
		clv_err("unknown command 0x%08X", head);
	}

	clv_debug("length: %u", tlv->length);

	for (i = 0; i < tlv->length; i++) {
		printf("0x%02X ", data[i]);
		if (!((i + 1) % 8))
			printf("\n");
	}
}

u8 *clv_server_create_hpd_cmd(u64 hpd_info, u32 *n)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_hpd;
	u32 size, size_hpd, size_map, *map, *head;
	u8 *p;

	size_map = CLV_CMD_MAP_SIZE;
	size_hpd = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_hpd + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CLV_CMD_HPD_SHIFT;

	tlv = (struct clv_tlv *)(p+sizeof(u32));
	tlv->tag = CLV_TAG_WIN;
	tlv->length = size_hpd + size_map;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	tlv_hpd = (struct clv_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CLV_TAG_MAP;
	tlv_map->length = CLV_CMD_MAP_SIZE - sizeof(struct clv_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CLV_CMD_HPD_SHIFT - CLV_CMD_OFFSET] = (u8 *)tlv_hpd - p;
	tlv_hpd->tag = CLV_TAG_RESULT;
	tlv_hpd->length = sizeof(u64);
	*((u64 *)(&tlv_hpd->payload[0])) = hpd_info;
	*n = size;

	return p;
}

u8 *clv_dup_hpd_cmd(u8 *dst, u8 *src, u32 n, u64 hpd_info)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_hpd;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct clv_tlv *)(dst+sizeof(u32));
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_hpd = (struct clv_tlv *)(dst
			+ map[CLV_CMD_HPD_SHIFT-CLV_CMD_OFFSET]);
	*((u32 *)(&tlv_hpd->payload[0])) = hpd_info;
	return dst;
}

u64 clv_client_parse_hpd_cmd(u8 *data)
{
	struct clv_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CLV_CMD_HPD_SHIFT)))
		return 0;

	tlv = (struct clv_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CLV_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct clv_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CLV_CMD_HPD_SHIFT - CLV_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct clv_tlv *)(data
			+ map[CLV_CMD_HPD_SHIFT-CLV_CMD_OFFSET]);
	if (tlv_result->tag != CLV_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

