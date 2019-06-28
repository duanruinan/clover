#include <stdlib.h>
#include <dlfcn.h>
#include <clover_utils.h>
#include <compositor.h>

#define LIB_NAME "libclover_gl_renderer.so"

static void *lib_handle = NULL;

static s32 (*create)(struct clv_compositor *c, s32 *formats, s32 count_fmts,
		     s32 no_winsys, void *native_window, s32 *vid) = NULL;
static void (*set_dbg)(u8 flag) = NULL;

static void load_lib(void)
{
	char *error;

	if (create && set_dbg) {
		return;
	} else {
		lib_handle = dlopen(LIB_NAME, RTLD_NOW);
		if (!lib_handle) {
			fprintf(stderr, "cannot load %s (%s)\n", LIB_NAME,
				dlerror());
			exit(EXIT_FAILURE);
		}
		dlerror();
		create = dlsym(lib_handle, "gl_renderer_create");
		error = dlerror();
		if (error)
			exit(EXIT_FAILURE);

		dlerror();
		set_dbg = dlsym(lib_handle, "gl_set_renderer_dbg");
		error = dlerror();
		if (error)
			exit(EXIT_FAILURE);
	}
}

s32 renderer_create(struct clv_compositor *c, s32 *formats, s32 count_fmts,
		    s32 no_winsys, void *native_window, s32 *vid)
{
	if (!create)
		load_lib();
	return create(c, formats, count_fmts, no_winsys, native_window, vid);
}

void set_renderer_dbg(u8 flag)
{
	if (!create)
		load_lib();
	set_dbg(flag);
}

