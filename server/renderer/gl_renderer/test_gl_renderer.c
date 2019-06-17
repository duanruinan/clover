#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_event.h>
#include <clover_region.h>
#include <clover_shm.h>
#include <compositor.h>

static Display *x_display = NULL;

s32 main(s32 argc, char **argv)
{
	struct clv_compositor compositor;

	x_display = XOpenDisplay(NULL);
	assert(x_display);
	gl_display_create(&compositor, NULL, 2, 0, x_display);
	return 0;
}

