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
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_region.h>
#include <clover_event.h>
#include <clover_signal.h>
#include <clover_compositor.h>

#define LIB_NAME "libclover_drm_backend.so"

static void *lib_handle = NULL;

static void (*set_dbg)(u8 flag) = NULL;
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

void set_backend_dbg(u8 flag)
{
	if (!set_dbg)
		load_lib();
	set_dbg(flag);
}

struct clv_event_loop *clv_display_get_event_loop(struct clv_display *display)
{
	return display->loop;
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
	INIT_LIST_HEAD(&display->clients);
	return display;

error:
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
	set_backend_dbg(255);
	clv_debug("create drm backend");
	c->backend = backend_create(c);
	if (!c->backend) {
		return -1;
	}	

	return 0;
}

static s32 output_repaint_timer_handler(void *data);

struct clv_compositor *clv_compositor_create(struct clv_display *display)
{
	struct clv_compositor *c = calloc(1, sizeof(*c));
	struct clv_event_loop *loop = clv_display_get_event_loop(display);

	if (!c)
		return NULL;

	c->display = display;

	c->repaint_msec = DEFAULT_REPAINT_MSEC;

	clv_signal_init(&c->destroy_signal);

	memset(&c->primary_plane, 0, sizeof(c->primary_plane));

	INIT_LIST_HEAD(&c->views);
	INIT_LIST_HEAD(&c->outputs);
	INIT_LIST_HEAD(&c->heads);
	INIT_LIST_HEAD(&c->planes);

	list_add_tail(&c->primary_plane.link, &c->planes);

	clv_signal_init(&c->output_created_signal);
	clv_signal_init(&c->output_destroyed_signal);
	clv_signal_init(&c->output_resized_signal);

	clv_signal_init(&c->heads_changed_signal);

	c->repaint_timer_source = clv_event_loop_add_timer(loop, 
					output_repaint_timer_handler, c);
	if (!c->repaint_timer_source) {
		clv_err("cannot create repaint timer.");
		goto error;
	}

	if (clv_compositor_backend_create(c) < 0) {
		clv_err("failed to create clover backend.");
		goto error;
	}

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

	clv_signal_emit(&c->heads_changed_signal, c);
}

struct clv_head *clv_compositor_enumerate_head(struct clv_compositor *c,
					       struct clv_head *last)
{
	struct clv_head *h = NULL;
	s32 f = 0;

	if (last == NULL) {
		list_for_each_entry(h, &c->heads, link) {
			return h;
		}
	} else {
		list_for_each_entry(h, &c->heads, link) {
			if (f)
				return h;
			if (h == last)
				f = 1;
		}
	}

	return NULL;
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

void clv_plane_init(struct clv_plane *plane, struct clv_compositor *c,
		    struct clv_plane *above)
{
	plane->c = c;
	if (above) {
		__list_add(&plane->link, above->link.prev, &above->link);
	} else {
		list_add(&plane->link, &c->planes);
	}
}

void clv_plane_deinit(struct clv_plane *plane)
{
	list_del(&plane->link);
}

void clv_output_deinit(struct clv_output *output)
{
	list_del(&output->link);
}

void clv_output_init(struct clv_output *output,
		     struct clv_compositor *c,
		     struct clv_rect *render_area,
		     u32 output_index,
		     struct clv_head *head)
{
	u32 *pixel;

	output->c = c;
	output->index = output_index;
	output->renderer_state = NULL;
	memcpy(&output->render_area, render_area, sizeof(*render_area));
	output->repaint_needed = 0;
	output->repainted = 0;
	output->repaint_status = REPAINT_NOT_SCHEDULED;
	clv_signal_init(&output->frame_signal);
	clv_signal_init(&output->destroy_signal);
	INIT_LIST_HEAD(&output->modes);
	output->head = head;
	output->enabled = 0;
	list_add_tail(&output->link, &c->outputs);

	memset(&output->bg_surf, 0, sizeof(output->bg_surf));
	output->bg_surf.c = c;
	output->bg_surf.is_opaque = 1;
	output->bg_surf.view = &output->bg_view;
	clv_region_init_rect(&output->bg_surf.damage, 0, 0, render_area->w,
			     render_area->h);
	
	output->bg_surf.w = render_area->w;
	output->bg_surf.h = render_area->h;
	clv_region_init_rect(&output->bg_surf.opaque, 0, 0, render_area->w,
			     render_area->h);
	clv_signal_init(&output->bg_surf.destroy_signal);

	memset(&output->bg_view, 0, sizeof(output->bg_view));
	output->bg_view.surface = &output->bg_surf;
	output->bg_view.type = CLV_VIEW_TYPE_PRIMARY;
	output->bg_view.area.pos.x = 0;
	output->bg_view.area.pos.y = 0;
	output->bg_view.area.w = render_area->w;
	output->bg_view.area.h = render_area->h;
	output->bg_view.alpha = 1.0f;
	list_add_tail(&output->bg_view.link, &c->views);

	memset(&output->bg_buf, 0, sizeof(output->bg_buf));
	output->bg_buf.base.type = CLV_BUF_TYPE_SHM;
	output->bg_buf.base.w = render_area->w;
	output->bg_buf.base.h = render_area->h;
	output->bg_buf.base.stride = render_area->w * 4;
	output->bg_buf.base.size = output->bg_buf.base.stride
					* output->bg_buf.base.h;
	output->bg_buf.base.pixel_fmt = CLV_PIXEL_FMT_ARGB8888;
	output->bg_buf.base.count_planes = 1;
	sprintf(output->bg_buf.base.name, "output_bg_%u", output->index);
	unlink(output->bg_buf.base.name);
	clv_shm_init(&output->bg_buf.shm, output->bg_buf.base.name,
		     output->bg_buf.base.size, 1);
	pixel = (u32 *)output->bg_buf.shm.map;;
	for (s32 i = 0; i < output->bg_buf.base.size / 4; i++)
//		pixel[i] = 0xFFFFFFFF;
		pixel[i] = 0xFF112233;
	c->renderer->attach_buffer(&output->bg_surf, &output->bg_buf.base);
	c->renderer->flush_damage(&output->bg_surf);
}

void clv_compositor_choose_head_best_size(struct clv_compositor *c, u32 *w,
					  u32 *h, u32 max_w, u32 max_h,
					  u32 head_index)
{
	struct clv_head *head;
	s32 f = 0;
	u32 width, height;
	void *last;

	list_for_each_entry(head, &c->heads, link) {
		if (head->index == head_index) {
			f = 1;
			break;
		}
	}
	assert(f);
	if (!head->connected)
		return;

	last = NULL;
	do {
		head->head_enumerate_mode(head, &width, &height, &last);
		clv_debug("width = %u height = %u", width, height);
		if (width == 0 || height == 0)
			break;
		if (max_w >= width && max_h >= height) {
			*w = width;
			*h = height;
			return;
		}
	} while (1);
}

void clv_compositor_choose_current_mode(struct clv_compositor *c, u32 w, u32 h,
					u32 output_index)
{
	struct clv_output *output;
	struct clv_mode *m;
	s32 f = 0;

	list_for_each_entry(output, &c->outputs, link) {
		if (output->index == output_index) {
			f = 1;
			break;
		}
	}
	assert(f);

	f = 0;
	list_for_each_entry(m, &output->modes, link) {
		if (m->w == w && m->h == h) {
			f = 1;
			break;
		}
	}
	assert(f);
	output->current_mode = m;
}

static void idle_repaint(void *data)
{
	struct clv_output *output = data;

	assert(output->repaint_status == REPAINT_BEGIN_FROM_IDLE);
	output->repaint_status = REPAINT_AWAITING_COMPLETION;
	output->idle_repaint_source = NULL;
	output->start_repaint_loop(output);
}

void clv_output_schedule_repaint(struct clv_output *output)
{
	struct clv_compositor *c = output->c;
	struct clv_event_loop *loop;

	if (!output->repaint_needed)
		clv_debug("core_repaint_req");

	loop = clv_display_get_event_loop(c->display);
	output->repaint_needed = 1;

	/* If we already have a repaint scheduled for our idle handler,
	 * no need to set it again. If the repaint has been called but
	 * not finished, then clv_output_finish_frame() will notice
	 * that a repaint is needed and schedule one. */
	if (output->repaint_status != REPAINT_NOT_SCHEDULED)
		return;

	output->repaint_status = REPAINT_BEGIN_FROM_IDLE;
	assert(!output->idle_repaint_source);
	output->idle_repaint_source = clv_event_loop_add_idle(loop,
							      idle_repaint,
							      output);
	clv_debug("core_repaint_enter_loop");
}

static void clv_output_schedule_repaint_reset(struct clv_output *output)
{
	output->repaint_status = REPAINT_NOT_SCHEDULED;
	clv_debug("core_repaint_exit_loop");
}

static s32 clv_output_repaint(struct clv_output *output, void *repaint_data)
{
	struct clv_compositor *c = output->c;
	struct clv_view *v;
	s32 r;

	clv_debug("core_repaint_begin");

	list_for_each_entry(v, &c->views, link) {
		v->plane = &c->primary_plane;
	}
	list_for_each_entry(v, &c->views, link) {
		c->renderer->flush_damage(v->surface);
	}

	r = output->repaint(output, repaint_data);

	output->repaint_needed = 0;
	if (r == 0)
		output->repaint_status = REPAINT_AWAITING_COMPLETION;

	clv_debug("core_repaint_posted");

	return r;
}

static s32 clv_output_maybe_repaint(struct clv_output *output,
				    struct timespec *now,
				    void *repaint_data)
{
	struct clv_compositor *c = output->c;
	s32 ret = 0;
	s64 msec_to_repaint;

	/* We're not ready yet; come back to make a decision later. */
	if (output->repaint_status != REPAINT_SCHEDULED)
		return ret;

	msec_to_repaint = timespec_sub_to_msec(&output->next_repaint, now);
	if (msec_to_repaint > 1)
		return ret;

	/* We don't actually need to repaint this output; drop it from
	 * repaint until something causes damage. */
	if (!output->repaint_needed)
		goto err;

	/* If repaint fails, we aren't going to get clv_output_finish_frame
	 * to trigger a new repaint, so drop it from repaint and hope
	 * something schedules a successful repaint later. As repainting may
	 * take some time, re-read our clock as a courtesy to the next
	 * output. */
	ret = clv_output_repaint(output, repaint_data);
	clock_gettime(c->clock_type, now);
	if (ret != 0)
		goto err;

	output->repainted = 1;
	return ret;

err:
	clv_output_schedule_repaint_reset(output);
	return ret;
}

static void output_repaint_timer_arm(struct clv_compositor *c)
{
	struct clv_output *output;
	s32 any_should_repaint = 0;
	struct timespec now;
	s64 msec_to_next = INT64_MAX;
	s64 msec_to_this;

	clv_debug("TAG");
	clock_gettime(c->clock_type, &now);

	list_for_each_entry(output, &c->outputs, link) {
		if (output->repaint_status != REPAINT_SCHEDULED)
			continue;
		msec_to_this = timespec_sub_to_msec(&output->next_repaint,
						    &now);
		if (!any_should_repaint || msec_to_this < msec_to_next)
			msec_to_next = msec_to_this;
		any_should_repaint = 1;
	}

	if (!any_should_repaint) {
		clv_debug("TAG");
		return;
	}

	/* Even if we should repaint immediately, add the minimum 1 ms delay.
	 * This is a workaround to allow coalescing multiple output repaints
	 * particularly from clv_output_finish_frame()
	 * into the same call, which would not happen if we called
	 * output_repaint_timer_handler() directly.
	 */
	if (msec_to_next < 1)
		msec_to_next = 1;

	clv_debug("TAG update timer %lu", msec_to_next);
	clv_event_source_timer_update(c->repaint_timer_source, msec_to_next, 0);
}

static s32 output_repaint_timer_handler(void *data)
{
	struct clv_compositor *c = data;
	struct clv_output *output;
	struct timespec now;
	void *repaint_data = NULL;
	s32 ret = 0;

	clv_debug("TAG");
	clock_gettime(c->clock_type, &now);

	if (c->backend->repaint_begin) {
		repaint_data = c->backend->repaint_begin(c);
		clv_debug("TAG >>>> repaint_data = %p", repaint_data);
	}

	clv_debug("TAG >>>> repaint_data = %p", repaint_data);
	list_for_each_entry(output, &c->outputs, link) {
		/* repainted flag is set here */
		ret = clv_output_maybe_repaint(output, &now, repaint_data);
		if (ret)
			break;
	}

	if (ret == 0) {
		/* flush damage */
		if (c->backend->repaint_flush)
			c->backend->repaint_flush(c, repaint_data);
	} else {
		/* repaint failed. */
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

void clv_output_finish_frame(struct clv_output *output,
			     const struct timespec *stamp,
			     u32 presented_flags)
{
	struct clv_compositor *c = output->c;
	s32 refresh_nsec;
	struct timespec now;
	s64 msec_rel;

	clv_debug("TAG");
	assert(output->repaint_status == REPAINT_AWAITING_COMPLETION);
	assert(stamp || (presented_flags & PRESENTATION_FEEDBACK_INVALID));

	clock_gettime(c->clock_type, &now);

	/* If we haven't been supplied any timestamp at all, we don't have a
	 * timebase to work against, so any delay just wastes time. Push a
	 * repaint as soon as possible so we can get on with it. */
	if (!stamp) {
		output->next_repaint = now;
		goto out;
	}

	clv_debug("core_repaint_finished");

	refresh_nsec = millihz_to_nsec(output->current_mode->refresh);

	output->frame_time = *stamp;

	timespec_add_nsec(&output->next_repaint, stamp, refresh_nsec);
	timespec_add_msec(&output->next_repaint, &output->next_repaint,
			  -c->repaint_msec);
	msec_rel = timespec_sub_to_msec(&output->next_repaint, &now);

	if (msec_rel < -1000 || msec_rel > 1000) {
		clv_warn("computed repaint delay is insane: %lld msec",
			 (long long) msec_rel);
		output->next_repaint = now;
	}

	/* Called from start_repaint_loop and restart happens already after
	 * the deadline given by repaint_msec? In that case we delay until
	 * the deadline of the next frame, to give clients a more predictable
	 * timing of the repaint cycle to lock on. */
	if (presented_flags == PRESENTATION_FEEDBACK_INVALID && msec_rel < 0) {
		while (timespec_sub_to_nsec(&output->next_repaint, &now) < 0) {
			timespec_add_nsec(&output->next_repaint,
					  &output->next_repaint,
					  refresh_nsec);
		}
	}

out:
	output->repaint_status = REPAINT_SCHEDULED;
	output_repaint_timer_arm(c);
}

void clv_output_disable(struct clv_output *output)
{
	/* Disable is called unconditionally also for not-enabled outputs,
	 * because at compositor start-up, if there is an output that is
	 * already on but the compositor wants to turn it off, we have to
	 * forward the turn-off to the backend so it knows to do it.
	 * The backend cannot initially turn off everything, because it
	 * would cause unnecessary mode-sets for all outputs the compositor
	 * wants to be on.
	 */
	if (output->disable(output) < 0)
		return;
}

