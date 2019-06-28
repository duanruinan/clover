#include <stdlib.h>
#include <dlfcn.h>
#include <clover_utils.h>
#include <clover_region.h>
#include <clover_event.h>
#include <compositor.h>

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

struct clv_event_loop *clv_server_get_event_loop(struct clv_server *server)
{
	return server->loop;
}

struct clv_server *clv_server_create(void)
{
	struct clv_server *server;

	server = calloc(1, sizeof(*server));
	if (!server)
		return NULL;

	server->exit = 0;
	server->loop = clv_event_loop_create();
	if (!server->loop)
		goto error;
	INIT_LIST_HEAD(&server->clients);
	return server;

error:
	return NULL;
}

void clv_server_run(struct clv_server *server)
{
	while (!server->exit) {
		clv_event_loop_dispatch(server->loop, -1);
	}
}

void clv_server_stop(struct clv_server *server)
{
	server->exit = 1;
}

void clv_server_destroy(struct clv_server *server)
{
	if (!server)
		return;

	if (server->loop) {
		clv_event_loop_destroy(server->loop);
		server->loop = NULL;
	}
	free(server);
}
