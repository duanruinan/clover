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
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_compositor.h>
#include <clover_region.h>

static char *strip(char *str, s32 len)
{
	char *buf, *p, *q, *l, *r;

	buf = malloc(len);
	if (!buf)
		return NULL;

	memset(buf, 0, len);
	p = buf;
	q = str;
	while (1) {
		l = strstr(q, "<!--");
		if (!l) {
			memcpy(p, q, strlen(q));
			break;
		}
		memcpy(p, q, l - q);
		p = p + (l - q);
		r = strstr(l + strlen("<!--"), "-->");
		assert(r);
		q = r + strlen("-->");
	}

	return buf;
}

static char *load_file(const char *xml)
{
	s32 fd, ret;
	char *buf, *buf_strip, *p;
	u32 len, byts_to_rd;

	fd = open(xml, O_RDONLY, 0644);
	if (fd < 0) {
		clv_err("xml = %s, error = %s", xml, strerror(errno));
		return NULL;
	}
	len = lseek(fd, 0, SEEK_END);
	
	byts_to_rd = len;
	lseek(fd, 0, SEEK_SET);
	buf = malloc(len);
	if (!buf) {
		close(fd);
		return NULL;
	}
	memset(buf, 0, len);
	p = buf;
	while (byts_to_rd) {
		ret = read(fd, p, byts_to_rd);
		if (ret <= 0)
			break;
		byts_to_rd -= ret;
		p += ret;
	}
	close(fd);

	buf_strip = strip(buf, len);
	free(buf);
	return buf_strip;
}

#define strip_blank(p) do { \
	while ((*(p)) == ' ') \
		(p)++; \
	while ((*(p)) == '\t') \
		(p)++; \
	while ((*(p)) == '\n') \
		(p)++; \
	break; \
} while (1)

static char *parse_layer_attrib(struct clv_layer_config *layer, char *buf)
{
	char *p, *q, *l, *r;
	char buffer[64];

	p = buf;
	p = strstr(p, "<hw_layer");
	assert(p);
	strip_blank(p);

	q = p;
	l = strstr(q, "index=\"");
	l = l + strlen("index=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	layer->index = atoi(buffer);
	/* printf("layer->index = %d\n", layer->index); */

	q = p;
	l = strstr(q, "type=\"");
	l = l + strlen("type=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	if (!strcmp(buffer, "primary"))
		layer->type = 1;
	else if (!strcmp(buffer, "overlay"))
		layer->type = 0;
	else if (!strcmp(buffer, "cursor"))
		layer->type = 2;
	/* printf("layer->type = %d\n", layer->type); */

	q = p;
	l = strstr(q, "count_formats=\"");
	l = l + strlen("count_formats=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	layer->count_formats = atoi(buffer);
	/* printf("layer->count_formats = %d\n", layer->count_formats); */

	strip_blank(r);
	return r;
}

static char *parse_layer_node(struct clv_layer_config *layer, char *buf)
{
	char *p, *q, *l, *r, *t;
	s32 i;

	r = NULL;
	p = buf;
	p = parse_layer_attrib(layer, p);
	for (i = 0; i < layer->count_formats; i++) {
		q = p;
		l = strstr(q, "<format>");
		assert(l);
		t = l + strlen("<format>");
		strip_blank(t);
		r = strstr(t, "</format>");
		assert(r);
		memcpy(layer->formats[i], t, r - t);
		/* printf("layer->formats[%d] = %s\n", i, layer->formats[i]); */
		r = r + strlen("</format>");
		strip_blank(r);
		p = r;
	}
	if (r) {
		r = strstr(r, "</hw_layer>");
		r += strlen("</hw_layer>");
		strip_blank(r);
		return r;
	} else {
		return NULL;
	}
}

static char *parse_output_attrib(struct clv_output_config *output, char *buf)
{
	char *p, *q, *l, *r;
	char buffer[64];

	p = buf;
	p = strstr(p, "<output");
	assert(p);
	strip_blank(p);

	q = p;
	l = strstr(q, "index=\"");
	l = l + strlen("index=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	output->index = atoi(buffer);
	/* printf("output->index = %d\n", output->index); */

	q = p;
	l = strstr(q, "max_width=\"");
	l = l + strlen("max_width=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	output->max_w = atoi(buffer);
	/* printf("output->max_w = %u\n", output->max_w); */

	q = p;
	l = strstr(q, "max_height=\"");
	l = l + strlen("max_height=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	output->max_h = atoi(buffer);
	/* printf("output->max_h = %u\n", output->max_h); */

	q = p;
	l = strstr(q, "count_layers=\"");
	assert(l);
	l = l + strlen("count_layers=\"");
	assert(l);
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	output->count_layers = atoi(buffer);
	/* printf("output->count_layers = %d\n", output->count_layers); */

	strip_blank(r);
	return r;
}

static char *parse_output_node(struct clv_output_config *output, char *buf)
{
	char *p;
	s32 i;
	struct clv_layer_config *layer;

	p = parse_output_attrib(output, buf);
	for (i = 0; i < output->count_layers; i++) {
		p = parse_layer_node(&output->layers[i], p);
	}

	INIT_LIST_HEAD(&output->overlay_layers);
	for (i = 0; i < output->count_layers; i++) {
		layer = &output->layers[i];
		if (layer->type == 2) {
			output->cursor_layer = &output->layers[i];
		} else if (layer->type == 1) {
			output->primary_layer = &output->layers[i];
		} else {
			list_add_tail(&layer->link, &output->overlay_layers);
		}
	}

	p = strstr(p, "</output>");
	assert(p);
	p = p + strlen("</output>");
	strip_blank(p);
	return p;
}

static char *parse_encoder_attrib(struct clv_encoder_config *encoder, char *buf)
{
	char *p, *q, *l, *r;
	char buffer[64];

	p = buf;
	p = strstr(p, "<encoder");
	assert(p);
	strip_blank(p);

	q = p;
	l = strstr(q, "index=\"");
	l = l + strlen("index=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	encoder->index = atoi(buffer);
	/* printf("encoder->index = %d\n", encoder->index); */

	strip_blank(r);
	r = strstr(r, "/>");
	assert(r);
	r = r + strlen("/>");
	strip_blank(r);
	return r;
}

static char *parse_encoder_node(struct clv_encoder_config *encoder, char *buf)
{
	char *p;

	p = parse_encoder_attrib(encoder, buf);
	return parse_output_node(&encoder->output, p);
}

static char *parse_head_attrib(struct clv_head_config *head, char *buf)
{
	char *p, *q, *l, *r;
	char buffer[64];

	p = buf;
	p = strstr(p, "<head");
	assert(p);
	strip_blank(p);

	q = p;
	l = strstr(q, "index=\"");
	l = l + strlen("index=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	head->index = atoi(buffer);
	/* printf("head->index = %d\n", head->index); */

	q = p;
	l = strstr(q, "max_width=\"");
	l = l + strlen("max_width=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	head->max_w = atoi(buffer);
	/* printf("head->max_w = %u\n", head->max_w); */

	q = p;
	l = strstr(q, "max_height=\"");
	l = l + strlen("max_height=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	head->max_h = atoi(buffer);
	/* printf("head->max_h = %u\n", head->max_h); */

	strip_blank(r);
	return r;
}

static char *parse_head_node(struct clv_head_config *head, char *buf)
{
	char *p, *q;

	p = parse_head_attrib(head, buf);
	p = parse_encoder_node(&head->encoder, p);
	q = strstr(p, "</head>");
	assert(q);
	p = q + strlen("</head>");
	strip_blank(p);
	return p;
}

static char *parse_clover_attrib(struct clv_config *config, char *buf)
{
	char *p, *q, *l, *r;
	char buffer[64];

	p = buf;
	p = strstr(p, "<clover");
	assert(p);
	strip_blank(p);

	q = p;
	l = strstr(q, "count_heads=\"");
	l = l + strlen("count_heads=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	config->count_heads = atoi(buffer);
	/* printf("config->count_heads = %d\n", config->count_heads); */

	q = p;
	l = strstr(q, "mode=\"");
	assert(l);
	l = l + strlen("mode=\"");
	strip_blank(l);
	r = strstr(l, "\"");
	memset(buffer, 0, 64);
	memcpy(buffer, l, r - l);
	if (!strcmp(buffer, "extended")) {
		config->mode = CLV_DESKTOP_EXTENDED;
		/* printf("EXTENDED\n"); */
	} else if (!strcmp(buffer, "duplicated")) {
		config->mode = CLV_DESKTOP_DUPLICATED;
		/* printf("DUPLICATED\n"); */
	} else {
		assert(0);
	}

	strip_blank(r);
	return r;
}

static void parse_config(struct clv_config *config, char *buf)
{
	char *p;
	s32 i;

	p = parse_clover_attrib(config, buf);
	for (i = 0; i < config->count_heads; i++) {
		p = parse_head_node(&config->heads[i], p);
	}
}

struct clv_config *load_config_from_file(const char *xml)
{
	char *buf = load_file(xml);
	struct clv_config *config;

	config = calloc(1, sizeof(*config));
	if (!config)
		return NULL;
	parse_config(config, buf);
	free(buf);
	return config;
}

/*
s32 main(s32 argc, char **argv)
{
	s32 i, j;
	struct clv_config *config = load_config_from_file(argv[1]);
	struct clv_head_config *head;
	struct clv_encoder_config *encoder;
	struct clv_output_config *output;
	struct clv_layer_config *layer;

	printf("mode: %s\n", config->mode == CLV_DESKTOP_DUPLICATED ? "DUP"
	       : "EXT");
	for (i = 0; i < config->count_heads; i++) {
		head = &config->heads[i];
		printf("Connector Index: %u Max W: %u, Max H: %u\n",
		       head->index, head->max_w, head->max_h);
		encoder = &head->encoder;
		printf("\tEncoder Index: %u\n", encoder->index);
		output = &encoder->output;
		printf("\tOutput Index: %u Max W: %u, Max H: %u\n",
		       output->index, output->max_w, output->max_h);
		layer = output->primary_layer;
		printf("\t\tPrimary layer's Index: %u Type: %u\n",
		       layer->index, layer->type);
		for (j = 0; j < layer->count_formats; j++)
			printf("\t\t\t%s\n", layer->formats[j]);
		layer = output->cursor_layer;
		printf("\t\tCursor layer's Index: %u Type: %u\n",
		       layer->index, layer->type);
		for (j = 0; j < layer->count_formats; j++)
			printf("\t\t\t%s\n", layer->formats[j]);
		list_for_each_entry(layer, &output->overlay_layers, link) {
			printf("\t\tOverlay layer's Index: %u Type: %u\n",
			       layer->index, layer->type);
			for (j = 0; j < layer->count_formats; j++)
				printf("\t\t\t%s\n", layer->formats[j]);
		}
	}
	free(config);
	return 0;
}
*/

