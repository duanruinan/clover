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

#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_region.h>
#include <clover_event.h>
#include <clover_signal.h>
#include <clover_ipc.h>
#include <clover_compositor.h>

#define LIB_NAME "libclover_drm_backend.so"

static void *lib_handle = NULL;

static void (*set_dbg)(u32 flag) = NULL;
static struct clv_backend * (*backend_create)(struct clv_compositor *c) = NULL;

static void load_lib(void)
{
	char *error, *backend_name = NULL;

	backend_name = getenv("CLOVER_BACKEND_LIB");

	if (set_dbg) {
		return;
	} else {
		if (backend_name)
			lib_handle = dlopen(backend_name, RTLD_NOW);
		else
			lib_handle = dlopen(LIB_NAME, RTLD_NOW);
		if (!lib_handle) {
			fprintf(stderr, "cannot load backend (%s)\n",dlerror());
			exit(EXIT_FAILURE);
		}

		dlerror();
		set_dbg = dlsym(lib_handle, "drm_set_dbg");
		error = dlerror();
		if (error)
			exit(EXIT_FAILURE);

		dlerror();
		backend_create = dlsym(lib_handle, "drm_backend_create");
		error = dlerror();
		if (error)
			exit(EXIT_FAILURE);
	}
}

void set_scanout_dbg(u32 flag)
{
	if (!set_dbg)
		load_lib();
	set_dbg(flag);
}

static u8 cmp_dbg = 0;
static u8 timer_dbg = 0;

void set_compositor_dbg(u32 flags)
{
	cmp_dbg = flags & 0x0F;
	timer_dbg = (flags >> 4) & 0x0F;
}

#define cmp_debug(fmt, ...) do { \
	if (cmp_dbg >= 3) { \
		clv_debug("[COMP] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define cmp_info(fmt, ...) do { \
	if (cmp_dbg >= 2) { \
		clv_info("[COMP] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define cmp_notice(fmt, ...) do { \
	if (cmp_dbg >= 1) { \
		clv_notice("[COMP] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define cmp_warn(fmt, ...) do { \
	clv_warn("[COMP] " fmt, ##__VA_ARGS__); \
} while (0);

#define cmp_err(fmt, ...) do { \
	clv_err("[COMP] " fmt, ##__VA_ARGS__); \
} while (0);

#define timer_debug(fmt, ...) do { \
	if (timer_dbg >= 3) { \
		clv_debug("[TS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define timer_info(fmt, ...) do { \
	if (timer_dbg >= 2) { \
		clv_info("[TS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define timer_notice(fmt, ...) do { \
	if (timer_dbg >= 1) { \
		clv_notice("[TS  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define timer_warn(fmt, ...) do { \
	clv_warn("[TS  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define timer_err(fmt, ...) do { \
	clv_err("[TS  ] " fmt, ##__VA_ARGS__); \
} while (0);

struct clv_event_loop *clv_display_get_event_loop(struct clv_display *display)
{
	return display->loop;
}

void clv_display_destroy(struct clv_display *display)
{
	if (!display)
		return;
	
	if (display->loop) {
		clv_event_loop_destroy(display->loop);
		display->loop = NULL;
	}
	free(display);
}

struct clv_display *clv_display_create(void)
{
	struct clv_display *display;

	display = calloc(1, sizeof(*display));
	if (!display)
		return NULL;

	display->exit = 0;
	display->loop = clv_event_loop_create();
	if (!display->loop)
		goto error;
	
	return display;

error:
	clv_display_destroy(display);
	return NULL;
}

void clv_display_run(struct clv_display *display)
{
	while (!display->exit) {
		clv_event_loop_dispatch(display->loop, -1);
	}
}

void clv_display_stop(struct clv_display *display)
{
	display->exit = 1;
}

void clv_compositor_destroy(struct clv_compositor *c)
{
	struct clv_plane *pl, *next;

	clv_signal_emit(&c->destroy_signal, c);

	if (c->backend) {
		c->backend->destroy(c);
		c->backend = NULL;
	}

	list_for_each_entry_safe(pl, next, &c->planes, link) {
		list_del(&pl->link);
		if (pl != &c->primary_plane) {
			/* TODO */
			free(pl);
		}
	}
}

static s32 clv_compositor_backend_create(struct clv_compositor *c)
{
	load_lib();
	cmp_debug("create drm backend");
	c->backend = backend_create(c);
	if (!c->backend) {
		return -1;
	}

	return 0;
}

/*
static void clv_compositor_init_dummy_cursor(struct clv_compositor *c)
{
	cmp_debug("init dummy cursor ...");
	memset(&c->dummy_cursor_surf, 0, sizeof(c->dummy_cursor_surf));
	c->dummy_cursor_surf.is_bg = 0;
	c->dummy_cursor_surf.c = c;
	c->dummy_cursor_surf.is_opaque = 0;
	c->dummy_cursor_surf.view = &c->dummy_cursor_view;
	clv_region_init_rect(&c->dummy_cursor_surf.damage, 0, 0, 32, 32);
	clv_region_init_rect(&c->dummy_cursor_surf.opaque, 0, 0, 32, 32);
	clv_signal_init(&c->dummy_cursor_surf.destroy_signal);
	c->dummy_cursor_surf.w = 32;
	c->dummy_cursor_surf.h = 32;

	memset(&c->dummy_cursor_view, 0, sizeof(c->dummy_cursor_view));
	c->dummy_cursor_view.surface = &c->dummy_cursor_surf;
	c->dummy_cursor_view.area.pos.x = 1919;
	c->dummy_cursor_view.area.pos.y = 0;
	c->dummy_cursor_view.area.w = 32;
	c->dummy_cursor_view.area.h = 32;
	c->dummy_cursor_view.alpha = 1.0f;
	c->dummy_cursor_view.output_mask = 0xFF;
	c->dummy_cursor_view.type = CLV_VIEW_TYPE_CURSOR;
	list_add_tail(&c->dummy_cursor_view.link, &c->views);
}
*/

static void clv_compositor_init_background(struct clv_compositor *c)
{
	u32 *pixel;

	cmp_debug("init background layer...");
	memset(&c->bg_surf, 0, sizeof(c->bg_surf));
	c->bg_surf.is_bg = 1;
	c->bg_surf.c = c;
	c->bg_surf.is_opaque = 1;
	c->bg_surf.view = &c->bg_view;
	clv_region_init_rect(&c->bg_surf.damage, 0, 0, 8192, 2160);
	clv_region_init_rect(&c->bg_surf.opaque, 0, 0, 8192, 2160);
	clv_signal_init(&c->bg_surf.destroy_signal);
	c->bg_surf.w = 8192;
	c->bg_surf.h = 2160;

	memset(&c->bg_view, 0, sizeof(c->bg_view));
	c->bg_view.surface = &c->bg_surf;
	c->bg_view.area.pos.x = 0;
	c->bg_view.area.pos.y = 0;
	c->bg_view.area.w = 8192;
	c->bg_view.area.h = 2160;
	c->bg_view.alpha = 1.0f;
	c->bg_view.output_mask = 0xFF;
	c->bg_view.type = CLV_VIEW_TYPE_PRIMARY;
	list_add_tail(&c->bg_view.link, &c->views);

	memset(&c->bg_buf, 0, sizeof(c->bg_buf));
	c->bg_buf.base.type = CLV_BUF_TYPE_SHM;
	c->bg_buf.base.w = 8192;
	c->bg_buf.base.h = 2160;
	c->bg_buf.base.stride = 8192 * 4;
	c->bg_buf.base.size = c->bg_buf.base.stride * c->bg_buf.base.h;
	c->bg_buf.base.pixel_fmt = CLV_PIXEL_FMT_ARGB8888;
	c->bg_buf.base.count_planes = 1;
	strcpy(c->bg_buf.base.name, "shm_clover_background");
	unlink(c->bg_buf.base.name);
	clv_shm_init(&c->bg_buf.shm, c->bg_buf.base.name,
		     c->bg_buf.base.size, 1);
	pixel = (u32 *)c->bg_buf.shm.map;
	for (s32 i = 0; i < c->bg_buf.base.size / 4; i++)
		pixel[i] = 0xFF404040;

	c->renderer->attach_buffer(&c->bg_surf, &c->bg_buf.base);
	c->renderer->flush_damage(&c->bg_surf);
}

static s32 output_repaint_timer_handler(void *data);

static s32 clover_delay = -11;

struct clv_compositor *clv_compositor_create(struct clv_display *display)
{
	struct clv_compositor *c = calloc(1, sizeof(*c));
	struct clv_event_loop *loop = clv_display_get_event_loop(display);
	char *delay_value;

	if (!c)
		return NULL;

	c->display = display;

	clv_signal_init(&c->destroy_signal);

	INIT_LIST_HEAD(&c->views);
	INIT_LIST_HEAD(&c->outputs);
	INIT_LIST_HEAD(&c->heads);
	INIT_LIST_HEAD(&c->planes);

	memset(&c->primary_plane, 0, sizeof(c->primary_plane));
	strcpy(c->primary_plane.name, "Root");
	list_add_tail(&c->primary_plane.link, &c->planes);

	c->repaint_timer = clv_event_loop_add_timer(loop,
					output_repaint_timer_handler, c);

	clv_signal_init(&c->heads_changed_signal);

	if (clv_compositor_backend_create(c) < 0) {
		cmp_err("failed to create clover backend.");
		goto error;
	}

	clv_compositor_init_background(c);
	//clv_compositor_init_dummy_cursor(c);

	delay_value = getenv("CLOVER_DELAY");
	if (delay_value)
		clover_delay = atoi(delay_value);
	clv_debug("CLOVER_DELAY: %d", clover_delay);

	return c;

error:
	clv_compositor_destroy(c);
	return NULL;
}

void clv_compositor_add_heads_changed_listener(struct clv_compositor *c,
					       struct clv_listener *listener)
{
	clv_signal_add(&c->heads_changed_signal, listener);
}

static void clv_compositor_heads_changed_cb(void *data)
{
	struct clv_compositor *c = data;

	c->heads_changed_source = NULL;

	cmp_debug("head change");
	clv_signal_emit(&c->heads_changed_signal, c);
}

void clv_compositor_schedule_heads_changed(struct clv_compositor *c)
{
	struct clv_event_loop *loop;

	if (c->heads_changed_source)
		return;

	loop = clv_display_get_event_loop(c->display);
	c->heads_changed_source = clv_event_loop_add_idle(loop,
					clv_compositor_heads_changed_cb,
					c);
}

/*
 * Set output->current_mode
 */
void clv_compositor_choose_mode_manually(struct clv_output *output,
					 struct clv_rect *rc,
					 enum timing_select_method method)
{
	u32 width, height;
	struct clv_mode *mode, *m, *mm;
	s32 f = 0;
	u32 refresh = 0;

	width = rc->w;
	height = rc->h;

	list_for_each_entry(mode, &output->modes, link) {
		if (mode->flags & MODE_PREFERRED) {
			f = 1;
			break;
		}
	}

	if (f) {
		if (mode->w <= width && mode->h <= height) {
			if (method == USE_HIGHVFREQ) {
				mm = NULL;
				list_for_each_entry(m, &output->modes, link) {
					if ((m->w == mode->w)
					    && (m->h == mode->h)
					    && (m->refresh > refresh)) {
						refresh = m->refresh;
						mm = m;
					}
				}
				if (mm) {
					output->current_mode = mm;
					cmp_debug("Use preferred mode %ux%u@%u",
				  		  m->w, m->h, m->refresh);
					return;
				}
			}
			output->current_mode = mode;
			cmp_debug("Use preferred mode %ux%u",
				  output->current_mode->w,
				  output->current_mode->h);
			return;
		}
	}

	list_for_each_entry(mode, &output->modes, link) {
		if (mode->w <= width && mode->h <= height) {
			output->current_mode = mode;
			cmp_debug("Use mode %ux%u",
				  output->current_mode->w,
				  output->current_mode->h);
			return;
		}
	}

	cmp_err("cannot choose mode");
	assert(0);
}

/*
 * Set output->current_mode
 */
void clv_compositor_choose_mode(struct clv_output *output,
				struct clv_head_config *head_cfg,
				enum timing_select_method method)
{
	u32 max_width, max_height;
	struct clv_mode *mode, *m, *mm;
	s32 f = 0;
	struct clv_output_config *output_cfg = &head_cfg->encoder.output;
	u32 refresh = 0;

	max_width = MAX(head_cfg->max_w, output_cfg->max_w);
	max_height = MAX(head_cfg->max_h, output_cfg->max_h);

	list_for_each_entry(mode, &output->modes, link) {
		if (mode->flags & MODE_PREFERRED) {
			f = 1;
			break;
		}
	}

	if (f) {
		if (mode->w <= max_width && mode->h <= max_height) {
			if (method == USE_HIGHVFREQ) {
				mm = NULL;
				list_for_each_entry(m, &output->modes, link) {
					if ((m->w == mode->w)
					    && (m->h == mode->h)
					    && (m->refresh > refresh)) {
						refresh = m->refresh;
						mm = m;
					}
				}
				if (mm) {
					output->current_mode = mm;
					cmp_debug("Use preferred mode %ux%u@%u",
				  		  m->w, m->h, m->refresh);
					return;
				}
			}
			output->current_mode = mode;
			cmp_debug("Use preferred mode %ux%u",
				  output->current_mode->w,
				  output->current_mode->h);
			return;
		}
	}

	list_for_each_entry(mode, &output->modes, link) {
		if (mode->w <= max_width && mode->h <= max_height) {
			output->current_mode = mode;
			cmp_debug("Use mode %ux%u",
				  output->current_mode->w,
				  output->current_mode->h);
			return;
		}
	}

	cmp_err("cannot choose mode");
	assert(0);
}

static void idle_repaint(void *data)
{
	struct clv_output *output = data;

	cmp_debug("idle repaint begin...");
	assert(output->repaint_status == REPAINT_BEGIN_FROM_IDLE);
	output->repaint_status = REPAINT_AWAITING_COMPLETION;
	output->idle_repaint_source = NULL;
	output->start_repaint_loop(output);
}

void clv_output_schedule_repaint(struct clv_output *output, s32 cnt)
{
	struct clv_compositor *c = output->c;
	struct clv_event_loop *loop;

	//printf("schedule output[%u]'s repaint\n", output->index);
	if (!output->enabled) {
		//cmp_warn("output %u is disabled.!", output->index);
		return;
	}
	cmp_debug("repaint scheduled...");
	output->primary_dirty = 1;
	loop = clv_display_get_event_loop(c->display);
	if (output->repaint_status != REPAINT_NOT_SCHEDULED) {
		//printf("output[%u]'s repaint already started.\n");
		cmp_info("repaint already scheduled! %u %d",
			 output->repaint_status, output->repaint_needed);
		output->repaint_pending = 1;
		return;
	}

	output->repaint_status = REPAINT_BEGIN_FROM_IDLE;
	assert(!output->idle_repaint_source);
	output->idle_repaint_source = clv_event_loop_add_idle(loop,
						idle_repaint,
						output);
	output->repaint_needed = cnt;
	cmp_debug("repaint loop begin...");
}

void clv_compositor_schedule_repaint(struct clv_compositor *c)
{
	struct clv_output *output;

	list_for_each_entry(output, &c->outputs, link)
		clv_output_schedule_repaint(output, 1);
}

void clv_surface_schedule_repaint(struct clv_surface *surface)
{
	struct clv_output *output;

	list_for_each_entry(output, &surface->c->outputs, link) {
		if (surface->output_mask & (1 << output->index)) {
			clv_output_schedule_repaint(output, 1);
		}
	}
}

void clv_view_schedule_repaint(struct clv_view *view)
{
	struct clv_output *output;

	//printf("view schedule repaint\n");
	list_for_each_entry(output, &view->surface->c->outputs, link) {
		if (view->output_mask & (1 << output->index)) {
			clv_output_schedule_repaint(output, 1);
		}
	}
}

static void output_repaint_timer_arm(struct clv_compositor *c)
{
	struct clv_output *output;
	s32 any_should_repaint = 0;
	struct timespec now;
	s64 msec_to_next = INT64_MAX;
	s64 msec_to_this;

	clock_gettime(c->clk_id, &now);
	list_for_each_entry(output, &c->outputs, link) {
		if (output->repaint_status != REPAINT_SCHEDULED)
			continue;
		msec_to_this = timespec_sub_to_msec(&output->next_repaint,
						    &now);
		if (!any_should_repaint || msec_to_this < msec_to_next) {
			msec_to_next = msec_to_this;
		}
		any_should_repaint = 1;
	}

	if (!any_should_repaint)
		return;

	if (msec_to_next < 1) {
		timer_info("msec_to_next = %ld", msec_to_next);
		msec_to_next = 1;
	}
	timer_debug("timer update to %ld", msec_to_next);
	clv_event_source_timer_update(c->repaint_timer, msec_to_next, 0);
}

void clv_output_finish_frame(struct clv_output *output, struct timespec *stamp)
{
	struct clv_compositor *c = output->c;
	struct timespec now;
	s32 refresh_nsec;
	s64 msec_rel;

	assert(output->repaint_status == REPAINT_AWAITING_COMPLETION);

	clock_gettime(c->clk_id, &now);
	timer_debug("[OUTPUT: %u] now: %ld, %ld",
		    output->index, now.tv_sec, now.tv_nsec / 1000000l);
	if (!stamp) {
		output->next_repaint = now;
		timer_debug("[OUTPUT: %u] set next_repaint to now %ld,%ld",
			    output->index, output->next_repaint.tv_sec,
			    output->next_repaint.tv_nsec / 1000000l);
		goto out;
	}
	refresh_nsec = millihz_to_nsec(output->current_mode->refresh);
	cmp_debug("************emit bo complete");
//	clv_debug("emit bo complete");
	cmp_debug("output %p %u", output, output->index);
//	clv_debug("----- emit flip event %p, %d", &output->flip_signal, output->index);
	if (list_empty(&output->flip_signal.listener_list)) {
		cmp_debug("empty!!!!!!!!!!!");
	} else {
		clv_signal_emit(&output->flip_signal, output);
	}
//	clv_debug("----- emit flip event over");
	timer_debug("[OUTPUT: %u] repaint finished! refresh: %u",
		    output->index, refresh_nsec / 1000000);
	timespec_add_nsec(&output->next_repaint, stamp, refresh_nsec);
	//TODO
	timespec_add_msec(&output->next_repaint, &output->next_repaint,
			clover_delay);
	msec_rel = timespec_sub_to_msec(&output->next_repaint, &now);
	if (msec_rel < -1000 || msec_rel > 1000) {
		timer_warn("[OUTPUT: %u] repaint delay is insane:%ld msec",
			   output->index, msec_rel);
		output->next_repaint = now;
	}
	if (msec_rel < 0) {
		timer_debug("[OUTPUT: %u] msec_rel < 0 %ld, next: %ld, %ld "
			   "now: %ld, %ld",
			   output->index, msec_rel, output->next_repaint.tv_sec,
			   output->next_repaint.tv_nsec / 1000000l,
			 now.tv_sec, now.tv_nsec / 1000000l);
		while (timespec_sub_to_nsec(&output->next_repaint, &now) < 0) {
			timespec_add_nsec(&output->next_repaint,
					  &output->next_repaint,
					  refresh_nsec);
		}
	}
	timer_debug("[OUTPUT: %u] msec_rel: %ld, next_repaint: %ld, %ld",
		    output->index, msec_rel, output->next_repaint.tv_sec,
		    output->next_repaint.tv_nsec / 1000000l);
out:
	output->repaint_status = REPAINT_SCHEDULED;
//	output->repaint_needed = 1;
	output_repaint_timer_arm(c);
}

void clv_output_schedule_repaint_reset(struct clv_output *output)
{
	//printf("schedule output[%u]'s repaint reset\n", output->index);
	output->repaint_status = REPAINT_NOT_SCHEDULED;
	cmp_debug("repaint loop exit.");
}

static s32 clv_output_repaint(struct clv_output *output, void *repaint_data)
{
	//struct clv_compositor *c = output->c;
	s32 ret;
	//struct timespec t1, t2;
	
	cmp_debug("output assign plane");
	if (output->assign_planes) {
		output->assign_planes(output, repaint_data);
	}

	cmp_debug("output repaint");
	//clock_gettime(c->clk_id, &t1);
	ret = output->repaint(output, repaint_data);
	//clock_gettime(c->clk_id, &t2);
	//printf("output repaint %u spent %ld ms\n", output->index,
	//	    timespec_sub_to_msec(&t2, &t1));
	if (output->repaint_needed)
		output->repaint_needed--;
	if (!ret)
		output->repaint_status = REPAINT_AWAITING_COMPLETION;

	return ret;
}

static s32 clv_output_maybe_repaint(struct clv_output *output,
				    struct timespec *now, void *repaint_data)
{
	//struct clv_compositor *c = output->c;
	s32 ret = 0;
	s64 msec_to_repaint;
	//struct timespec t1, t2;

	if (output->repaint_status != REPAINT_SCHEDULED)
		return ret;

	msec_to_repaint = timespec_sub_to_msec(&output->next_repaint, now);
	if (msec_to_repaint > 1)
		return ret;

	cmp_debug("repaint_needed = %d", output->repaint_needed);
	//clock_gettime(c->clk_id, &t1);
	if (!output->repaint_needed) {
		if (output->repaint_pending) {
			cmp_debug("there are repaint event pending.");
			output->repaint_pending = 0;
			output->repaint_needed = 1;
		} else {
			cmp_debug("do not need repaint");
			goto error;
		}
	}

	ret = clv_output_repaint(output, repaint_data);
	//clock_gettime(c->clk_id, now);
	//timer_debug("render %u spent %ld ms", output->index,
	//	    timespec_sub_to_msec(now, &t1));
	//printf("render %u spent %ld ms\n", output->index,
	//	    timespec_sub_to_msec(now, &t1));
	if (ret)
		goto error;

	output->repainted = 1;
	timer_debug("output [%u] render complete.", output->index);
	return ret;

error:
	clv_output_schedule_repaint_reset(output);
	return ret;
}

static s32 output_repaint_timer_handler(void *data)
{
	struct clv_compositor *c = data;
	struct clv_output *output;
	struct timespec now, t1, t2;
	void *repaint_data;
	s32 ret = 0;

	clock_gettime(c->clk_id, &now);
	timer_debug("timer handler %ld, %ld...",
		    now.tv_sec, now.tv_nsec / 1000000l);
	if (c->backend->repaint_begin)
		repaint_data = c->backend->repaint_begin(c);

	list_for_each_entry(output, &c->outputs, link) {
		if (!output->enabled)
			continue;
		if (!output->render_area.w || !output->render_area.h)
			continue;
		ret = clv_output_maybe_repaint(output, &now, repaint_data);
		if (ret)
			break;
	}

	if (ret == 0) {
		clock_gettime(c->clk_id, &t1);
		if (c->backend->repaint_flush)
			c->backend->repaint_flush(c, repaint_data);
		clock_gettime(c->clk_id, &t2);
		timer_debug("scanout spent %ld ms",
			    timespec_sub_to_msec(&t2, &t1));
	} else {
		list_for_each_entry(output, &c->outputs, link) {
			if (output->repainted)
				clv_output_schedule_repaint_reset(output);
		}
		if (c->backend->repaint_cancel)
			c->backend->repaint_cancel(c, repaint_data);
	}

	list_for_each_entry(output, &c->outputs, link)
		output->repainted = 0;
	output_repaint_timer_arm(c);

	return 0;
}

void clv_surface_destroy(struct clv_surface *s)
{
/*
	clv_debug("----- destroy surface: %p", s);
	clv_debug("----- del flip_listener %p", &s->flip_listener);
	{
		struct clv_listener *li;
		s32 cnt = 0;

		list_for_each_entry(li, &s->primary_output->flip_signal.listener_list, link) {
			cnt++;
			clv_debug("----- B listener %p", li);
			if (cnt > 3) {
				break;
			}
		}
	}
	clv_debug("----- before del %p -> %p -> %p -> %p -> %p",
	     &s->primary_output->flip_signal.listener_list,
	     s->primary_output->flip_signal.listener_list.next,
	     s->primary_output->flip_signal.listener_list.next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next->next);
*/
	list_del(&s->flip_listener.link);
	INIT_LIST_HEAD(&s->flip_listener.link);
/*
	clv_debug("----- after del %p -> %p -> %p -> %p -> %p",
	     &s->primary_output->flip_signal.listener_list,
	     s->primary_output->flip_signal.listener_list.next,
	     s->primary_output->flip_signal.listener_list.next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next->next);
	{
		struct clv_listener *li;
		s32 cnt = 0;

		list_for_each_entry(li, &s->primary_output->flip_signal.listener_list, link) {
			cnt++;
			clv_debug("----- A listener %p", li);
			if (cnt > 3) {
				break;
			}
		}
	}
*/
	INIT_LIST_HEAD(&s->flip_listener.link);
	clv_signal_emit(&s->destroy_signal, NULL);

	if (s->view) {
		s->view->surface = NULL;
	}
	clv_region_fini(&s->opaque);
	clv_region_fini(&s->damage);
	s->view = NULL;
	memset(s, 0, sizeof(*s));
	free(s);
//	clv_debug("----- destroy surface over");
}

void clv_view_destroy(struct clv_view *v)
{
//	clv_debug("----- destroy view: %p", v);
	if (v->surface) {
		v->surface->view = NULL;
	}
	list_del(&v->link);
	free(v);
}

static void surface_flip_proc(struct clv_listener *listener, void *data)
{
	struct clv_surface *s = container_of(listener, struct clv_surface,
					     flip_listener);
	s32 sock = s->agent->sock;
	//struct clv_buffer *buffer, *next;
	//struct clv_output *output = data;
	//u32 output_mask = s->view->output_mask;
	//struct clv_compositor *c = output->c;
	s32 ret;
/*
	clv_debug("------ check surface %p's view %p", s, s->view);
	clv_debug("----- listener = %p", listener);
*/
	if (!s->view) {
		if (s) {
			clv_err("surface %p, %ux%u", s, s->w, s->h);
		}
		clv_err("!s->view");
		assert(0);
		return;
	}

	if (s->view->need_to_draw) {
//		clv_debug(" ----- need to draw");
		return;
	}

	(void)clv_dup_bo_complete_cmd(s->agent->bo_complete_tx_cmd,
				      s->agent->bo_complete_tx_cmd_t,
				      s->agent->bo_complete_tx_len,
				      0);
	assert(s->view);
	if (s->view->painted) {
		s->view->painted = 0;
		list_del(&listener->link);
		INIT_LIST_HEAD(&listener->link);
		cmp_debug("************ Send bo complete sock = %d", sock);
		//printf("************ Send bo complete sock = %d\n", sock);
		ret = clv_send(sock, s->agent->bo_complete_tx_cmd,
				s->agent->bo_complete_tx_len);
		if (ret < 0) {
			cmp_info("send bo complete failed. destroy agent.");
			client_agent_destroy(s->agent);
		}
	}
//	clv_debug("------ check surface over");
}

void clv_surface_add_flip_listener(struct clv_surface *s)
{
	struct clv_compositor *c = s->c;
	struct clv_output *output;
	struct clv_listener *li;

	s->flip_listener.notify = surface_flip_proc;
	if (!s->primary_output->enabled) {
		list_del(&s->flip_listener.link);
		INIT_LIST_HEAD(&s->flip_listener.link);
		list_for_each_entry(output, &c->outputs, link) {
			if (output->enabled) {
				s->primary_output = output;
				cmp_warn("surface's primary_output routed."
					 "surf(%p %ux%u %d 0x%02X",
					 s, s->w, s->h,
					 s->primary_output->index,
					 s->output_mask);
				break;
			}
		}
	}
/*
	clv_debug("----- ADD %ux%u %p to surface %p output %p (%d) flip_signal %p",
		  s->w, s->h,
		  &s->flip_listener, s, s->primary_output, s->primary_output->index,
		  &s->primary_output->flip_signal);
	clv_debug("----- before add %p -> %p -> %p -> %p -> %p",
	     &s->primary_output->flip_signal.listener_list,
	     s->primary_output->flip_signal.listener_list.next,
	     s->primary_output->flip_signal.listener_list.next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next->next);
*/
	list_for_each_entry(li, &s->primary_output->flip_signal.listener_list,
				link) {
		if (li == &s->flip_listener) {
			cmp_err("already added flip listener, skip!"
				"surf %p %ux%u %d %02X", s, s->w, s->h,
				s->primary_output->index, s->output_mask);
			return;
		}
	}
	clv_signal_add(&s->primary_output->flip_signal, &s->flip_listener);
/*
	clv_debug("----- after add %p -> %p -> %p -> %p -> %p",
	     &s->primary_output->flip_signal.listener_list,
	     s->primary_output->flip_signal.listener_list.next,
	     s->primary_output->flip_signal.listener_list.next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next,
	     s->primary_output->flip_signal.listener_list.next->next->next->next);
*/
	cmp_debug("Set view %p's need_to_draw as 1", s->view);
//	clv_debug("----- Set view %p's need_to_draw as 1", s->view);
	s->view->need_to_draw = 1;
/*
	{
		s32 cnt = 0;

		list_for_each_entry(li, &s->primary_output->flip_signal.listener_list, link) {
			cnt++;
			clv_debug("----- listener %p", li);
			if (cnt > 3) {
				break;
			}
		}
	}
*/
	if (list_empty(&s->primary_output->flip_signal.listener_list))
		cmp_debug("empty!!!!!!!!!!!");
	cmp_debug("output %p %u", s->primary_output, s->primary_output->index);
}

struct clv_surface *clv_surface_create(struct clv_compositor *c,
				       struct clv_surface_info *si,
				       struct clv_client_agent *agent)
{
	struct clv_surface *s = calloc(1, sizeof(*s));

	if (!s)
		return NULL;

	s->c = c;
	s->is_opaque = si->is_opaque;
	clv_signal_init(&s->destroy_signal);
	s->view = NULL;
	clv_region_init_rect(&s->damage, si->damage.pos.x, si->damage.pos.y,
			     si->damage.w, si->damage.h);
	clv_region_init_rect(&s->opaque, si->opaque.pos.x, si->opaque.pos.y,
			     si->opaque.w, si->opaque.h);
	s->w = si->width;
	s->h = si->height;

	s->agent = agent;
	s->is_bg = 0;

	INIT_LIST_HEAD(&s->flip_listener.link);

//	clv_debug("----- create surface %p", s);

	return s;
}

struct clv_view *clv_view_create(struct clv_surface *s,
				 struct clv_view_info *vi)
{
	struct clv_view *v = calloc(1, sizeof(*v));
	struct clv_output *output;

	assert(s);
	if (!v)
		return NULL;

	v->type = vi->type;
	v->plane = NULL;
	v->surface = s;
	v->surface->output_mask = vi->output_mask;
	s->view = v;
	memcpy(&v->area, &vi->area, sizeof(v->area));
	v->alpha = vi->alpha;
	v->output_mask = vi->output_mask;
	list_add_tail(&v->link, &s->c->views);

	list_for_each_entry(output, &s->c->outputs, link) {
		if (output->index == vi->primary_output) {
			s->primary_output = output;
			break;
		}
	}
//	clv_debug("----- create view %p", v);
	if (s->primary_output)
		return v;

	cmp_warn("cannot find primary output, choose the first output.");
	list_for_each_entry(output, &s->c->outputs, link) {
		s->primary_output = output;
		return v;
	}

	return NULL;
}

void shm_buffer_destroy(struct clv_buffer *buffer)
{
	struct shm_buffer *shm_buf = container_of(buffer, struct shm_buffer,
						  base);

	if (!shm_buf)
		return;

	clv_shm_release(&shm_buf->shm);
	free(shm_buf);
}

struct clv_buffer *shm_buffer_create(struct clv_bo_info *bi)
{
	struct shm_buffer *buffer;

	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return NULL;

	buffer->base.type = CLV_BUF_TYPE_SHM;
	buffer->base.w = bi->width;
	buffer->base.h = bi->height;
	buffer->base.size = bi->stride * bi->height;
	buffer->base.stride = bi->stride;
	buffer->base.pixel_fmt = bi->fmt;
	buffer->base.count_planes = bi->count_planes;
	strcpy(buffer->base.name, bi->name);
	clv_shm_init(&buffer->shm, bi->name, buffer->base.size, 0);
	INIT_LIST_HEAD(&buffer->base.link);
	return &buffer->base;
}

void client_destroy_buf(struct clv_client_agent *agent, struct clv_buffer *buf)
{
	s32 is_overlay = 0;
	struct clv_compositor *c = agent->c;

	if (!buf->link.prev || !buf->link.next) {
		clv_err("illegal link!!!!!!!!!!!!!!!!!");
		assert(0);
	}
	list_del(&buf->link);
	if (agent->view->type == CLV_VIEW_TYPE_OVERLAY)
		is_overlay = 1;
	if (buf->type == CLV_BUF_TYPE_DMA) {
		if (is_overlay) {
//			clv_debug("release dma buf %d", buf->fd);
			cmp_debug("release drm dma buf! %p",
				  buf->internal_fb);
			close(buf->fd);
			c->backend->dmabuf_destroy(
				agent->surface->primary_output,
				buf->internal_fb);
			free(buf);
		} else {
			cmp_debug("release gl dma buf");
			c->renderer->release_dmabuf(c, buf);
		}
	} else {
		cmp_debug("release shm buf");
		shm_buffer_destroy(buf);
	}
}

void client_agent_destroy(struct clv_client_agent *agent)
{
	struct clv_buffer *buffer, *next;
	struct clv_compositor *c = agent->c;
	struct clv_output *output;
	u32 output_mask;
	s32 is_overlay = 0;

	close(agent->sock);
	clv_event_source_remove(agent->client_source);
	if (!agent->view)
		goto out;
	output_mask = agent->view->output_mask;
	/* destroy view */
	if (agent->view->type == CLV_VIEW_TYPE_OVERLAY)
		is_overlay = 1;
	clv_view_destroy(agent->view);
	//printf("client destroy schedule output's repaint\n");
	list_for_each_entry(output, &c->outputs, link) {
		if (output_mask & (1 << output->index)) {
			clv_output_schedule_repaint(output, 1);
		}
	}
	/* destroy buffers */
	list_for_each_entry_safe(buffer, next, &agent->buffers, link) {
		list_del(&buffer->link);
		if (buffer->type == CLV_BUF_TYPE_DMA) {
			if (is_overlay) {
				cmp_debug("release drm dma buf! %p",
					buffer->internal_fb);
				close(buffer->fd);
				c->backend->dmabuf_destroy(
					agent->surface->primary_output,
					buffer->internal_fb);
				free(buffer);
			} else {
				cmp_debug("release gl dma buf");
				c->renderer->release_dmabuf(c, buffer);
			}
		} else if (buffer->type == CLV_BUF_TYPE_SHM) {
			cmp_debug("release shm buf");
			shm_buffer_destroy(buffer);
		}
	}
	/* destroy surface */
	clv_surface_destroy(agent->surface);
out:
	list_del(&agent->link);
	free(agent);
}

struct clv_client_agent *client_agent_create(
	struct clv_server *s,
	s32 sock,
	s32 (*client_sock_cb)(s32 fd, u32 mask, void *data))
{
	struct clv_event_loop *loop = clv_display_get_event_loop(s->c->display);
	struct clv_client_agent *agent = calloc(1, sizeof(*agent));
	u32 n;

	if (!agent)
		return NULL;

	agent->ipc_rx_buf_sz = 32 * 1024;
	agent->ipc_rx_buf = malloc(agent->ipc_rx_buf_sz);
	if (!agent->ipc_rx_buf) {
		free(agent);
		return NULL;
	}

	agent->surface_id_created_tx_cmd_t
		= clv_server_create_surface_id_cmd(0, &n);
	assert(agent->surface_id_created_tx_cmd_t);
	agent->surface_id_created_tx_cmd = malloc(n);
	assert(agent->surface_id_created_tx_cmd);
	agent->surface_id_created_tx_len = n;

	agent->view_id_created_tx_cmd_t
		= clv_server_create_view_id_cmd(0, &n);
	assert(agent->view_id_created_tx_cmd_t);
	agent->view_id_created_tx_cmd = malloc(n);
	assert(agent->view_id_created_tx_cmd);
	agent->view_id_created_tx_len = n;

	agent->bo_id_created_tx_cmd_t
		= clv_server_create_bo_id_cmd(0, &n);
	assert(agent->bo_id_created_tx_cmd_t);
	agent->bo_id_created_tx_cmd = malloc(n);
	assert(agent->bo_id_created_tx_cmd);
	agent->bo_id_created_tx_len = n;

	agent->commit_ack_tx_cmd_t
		= clv_server_create_commit_ack_cmd(0, &n);
	assert(agent->commit_ack_tx_cmd_t);
	agent->commit_ack_tx_cmd = malloc(n);
	assert(agent->commit_ack_tx_cmd);
	agent->commit_ack_tx_len = n;

	agent->bo_complete_tx_cmd_t
		= clv_server_create_bo_complete_cmd(0, &n);
	assert(agent->bo_complete_tx_cmd_t);
	agent->bo_complete_tx_cmd = malloc(n);
	assert(agent->bo_complete_tx_cmd);
	agent->bo_complete_tx_len = n;

	agent->hpd_tx_cmd_t
		= clv_server_create_hpd_cmd(0, &n);
	assert(agent->hpd_tx_cmd_t);
	agent->hpd_tx_cmd = malloc(n);
	assert(agent->hpd_tx_cmd);
	agent->hpd_tx_len = n;

	agent->destroy_ack_tx_cmd_t
		= clv_server_create_destroy_ack_cmd(0, &n);
	assert(agent->destroy_ack_tx_cmd_t);
	agent->destroy_ack_tx_cmd = malloc(n);
	assert(agent->destroy_ack_tx_cmd);
	agent->destroy_ack_tx_len = n;

	agent->c = s->c;
	agent->sock = sock;
	INIT_LIST_HEAD(&agent->link);
	agent->surface = NULL;
	agent->view = NULL;
	INIT_LIST_HEAD(&agent->buffers);
	agent->client_source = clv_event_loop_add_fd(loop, sock,
						     CLV_EVT_READABLE,
						     client_sock_cb,
						     agent);
	assert(agent->client_source);
	agent->f = 1;
	list_add_tail(&agent->link, &s->client_agents);

	return agent;
}

