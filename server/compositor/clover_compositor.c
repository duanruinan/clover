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
#include <clover_utils.h>
#include <clover_region.h>
#include <clover_event.h>
#include <clover_signal.h>
#include <clover_compositor.h>

#define LIB_NAME "libclover_drm_backend.so"

static void *lib_handle = NULL;

static void (*set_dbg)(u8 flag) = NULL;

static void load_lib(void)
{
	char *error;

	if (set_dbg) {
		return;
	} else {
		lib_handle = dlopen(LIB_NAME, RTLD_NOW);
		if (!lib_handle) {
			fprintf(stderr, "cannot load %s (%s)\n", LIB_NAME,
				dlerror());
			exit(EXIT_FAILURE);
		}

		dlerror();
		set_dbg = dlsym(lib_handle, "drm_set_dbg");
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

struct clv_compositor *clv_compositor_create(struct clv_display *display)
{
	struct clv_compositor *compositor = calloc(1, sizeof(*compositor));

	if (!compositor)
		return NULL;

	compositor->display = display;
	clv_signal_init(&compositor->destroy_signal);

	clv_signal_init(&compositor->output_created_signal);
	clv_signal_init(&compositor->output_destroyed_signal);
	clv_signal_init(&compositor->output_resized_signal);

	clv_signal_init(&compositor->head_changed_signal);

	return compositor;
}

