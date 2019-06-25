#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#if USE_FULL_GL
#include "gl_wrap.h"  /* use full OpenGL */
#else
#include <GLES2/gl2.h>  /* use OpenGL ES 2.x */
#endif
#include <EGL/egl.h>
#include <clover_utils.h>
#include <compositor.h>

//#define USE_RGB_BG 1
#define USE_YUV420_BG 1
//#define USE_YUV444_BG 1

struct display {
   struct clv_compositor compositor;
   struct clv_output output;
   struct clv_surface surface1;
   struct clv_surface surface2;
   struct clv_view view1;
   struct clv_view view2;
   struct shm_buffer buffers1;
   struct shm_buffer buffers2;
} g_disp;

static void
draw(s32 x, s32 y, u32 w1, u32 h1, u32 w2, u32 h2)
{
   struct clv_compositor *c = &g_disp.compositor;
   struct clv_surface *surface1 = &g_disp.surface1;
   struct clv_surface *surface2 = &g_disp.surface2;
   struct clv_view *view1 = &g_disp.view1;
   struct clv_view *view2 = &g_disp.view2;
   struct clv_output *output = &g_disp.output;
   struct shm_buffer *pbuf1;
   struct shm_buffer *pbuf2;
   static u32 last_w1, last_h1;
   u8 *data;
   u8 *data2;
   u32 *pixel;
   u32 *pixel2;
   static u32 color = 0xFFFFFF00;
   static u32 color2 = 0xFF0000FF;
   s32 i;

   if (!last_w1) {
      last_w1 = w1;
      last_h1 = h1;
   }
   
#ifdef USE_RGB_BG
   pbuf1 = &g_disp.buffers1;
   data2 = pbuf1->shm.map;
   pixel2 = (u32 *)data2;
   if (color2 == 0xFF000000)
      color2 = 0xFF0000FF;
   else
      color2 = color2 - 0x00000001;
   for (i = 0; i < pbuf1->base.size / 4; i++) {
      pixel2[i] = color2;
   }
#endif
#ifdef USE_YUV444_BG
   pbuf1 = &g_disp.buffers1;
   data2 = pbuf1->shm.map;
   memset(data2, 0, pbuf1->base.size);
#endif
#ifdef USE_YUV420_BG
   pbuf1 = &g_disp.buffers1;
   data2 = pbuf1->shm.map;
   memset(data2, 0, pbuf1->base.size);
#endif
   if (last_w1 != w1 || last_h1 != h1) {
      printf("resize %u %u !!!\n", w1, h1);
      output->render_area.w = w1;
      output->render_area.h = h1;
      surface1->w = w1;
      surface1->h = h1;
      view1->area.w = w1;
      view1->area.h = h1;
      pbuf1->base.w = w1;
#ifdef USE_RGB_BG
      pbuf1->base.stride = w1 * 4;
#else
      pbuf1->base.stride = w1;
#endif
      pbuf1->base.h = h1;
      clv_region_fini(&surface1->damage);
      clv_region_init_rect(&surface1->damage, w1, h1, w1, h1);
      last_w1 = w1;
      last_h1 = h1;
      clv_region_fini(&surface1->opaque);
      clv_region_init_rect(&surface1->opaque, 0, 0, w1, h1);
#ifdef USE_RGB_BG
      color2 = 0xFF0000FF;
      for (i = 0; i < pbuf1->base.size / 4; i++)
         pixel2[i] = color2;
#endif
   } else {
      clv_region_fini(&surface1->damage);
      clv_region_init_rect(&surface1->damage, w1/10, h1/10, w1/5*4, h1/5*4);
   }
   c->renderer->attach_buffer(surface1, &pbuf1->base);
   c->renderer->flush_damage(surface1);

   view2->area.pos.x = x;
   view2->area.pos.y = y;
//   printf("x = %d, y = %d\n", x, y);
   pbuf2 = &g_disp.buffers2;
   data = pbuf2->shm.map;
   pixel = (u32 *)data;
   if (color == 0xFF010000)
      color = 0xFFFFFF00;
   else
      color = color - 0x00010000;
   for (i = 0; i < pbuf2->base.size / 4; i++) {
      pixel[i] = color;
   }
   clv_region_fini(&surface2->damage);
   clv_region_init_rect(&surface2->damage, w2/4, h2/4, w2/2, h2/2);
   c->renderer->attach_buffer(surface2, &pbuf2->base);
   c->renderer->flush_damage(surface2);
   
   c->renderer->repaint_output(output);
   //getchar();
   //g_disp.buf_index = 1 - g_disp.buf_index;
}

/* new window size or exposure */
static void
reshape(int width, int height)
{
//   glViewport(0, 0, (GLint) width, (GLint) height);
}

static void
init(Display *x_dpy, Window *win, u32 win_w, u32 win_h, u32 w1, u32 h1,
	u32 w2, u32 h2,
	EGLint *vid)
{
   //typedef void (*proc)();
   struct clv_compositor *c = &g_disp.compositor;
   struct clv_surface *surface1 = &g_disp.surface1;
   struct clv_surface *surface2 = &g_disp.surface2;
   struct clv_view *view1 = &g_disp.view1;
   struct clv_view *view2 = &g_disp.view2;
   struct clv_output *output = &g_disp.output;
   struct shm_buffer *pbuf1;
   struct shm_buffer *pbuf2;
   s32 i, j;
   u8 *data;
   u32 *pixel;

   pbuf1 = &g_disp.buffers1;
   pbuf2 = &g_disp.buffers2;

   unlink("/dev/shm/testshm0");
   unlink("/dev/shm/testshm1");

   memset(c, 0, sizeof(*c));
   INIT_LIST_HEAD(&c->views);
   INIT_LIST_HEAD(&c->outputs);

   memset(output, 0, sizeof(*output));
   output->c = c;
   output->render_area.pos.x = 0;
   output->render_area.pos.y = 0;
   output->render_area.w = win_w;
   output->render_area.h = win_h;
   list_add_tail(&output->link, &c->outputs);

   memset(surface1, 0, sizeof(*surface1));
   surface1->c = c;
   surface1->is_opaque = 0;
   surface1->view = view1;
   clv_region_init_rect(&surface1->damage, 0, 0, w1, h1);
   surface1->w = w1;
   surface1->h = h1;
   clv_region_init_rect(&surface1->opaque, 0, 0, w1, h1);
   clv_signal_init(&surface1->destroy_signal);

   memset(surface2, 0, sizeof(*surface2));
   surface2->c = c;
   surface2->is_opaque = 0;
   surface2->view = view2;
   clv_region_init_rect(&surface2->damage, 0, 0, w2, h2);
   surface2->w = w2;
   surface2->h = h2;
   clv_region_init_rect(&surface2->opaque, 0, 0, w2, h2);
   clv_signal_init(&surface2->destroy_signal);

   memset(view1, 0, sizeof(*view1));
   view1->surface = surface1;
   view1->type = CLV_VIEW_TYPE_PRIMARY;
   view1->area.pos.x = 0;
   view1->area.pos.y = 0;
   view1->area.w = w1;
   view1->area.h = h1;
   view1->alpha = 1.0f;
   list_add_tail(&view1->link, &c->views);

   memset(view2, 0, sizeof(*view2));
   view2->surface = surface2;
   view2->type = CLV_VIEW_TYPE_PRIMARY;
   view2->area.pos.x = 0;
   view2->area.pos.y = 0;
   view2->area.w = w2;
   view2->area.h = h2;
   view2->alpha = 1.0f;
   list_add_tail(&view2->link, &c->views);

   memset(pbuf1, 0, sizeof(*pbuf1));
   pbuf1->base.type = CLV_BUF_TYPE_SHM;
   pbuf1->base.w = w1;
   pbuf1->base.h = h1;
#ifdef USE_YUV444_BG
   pbuf1->base.stride = w1;
   pbuf1->base.size = 1920 * 1080 * 3;
   pbuf1->base.pixel_fmt = CLV_PIXEL_FMT_YUV444P;
   pbuf1->base.count_planes = 3;
   sprintf(pbuf1->base.name, "testshm%d", 0);
   clv_shm_init(&pbuf1->shm, pbuf1->base.name, pbuf1->base.size, 1);
   data = pbuf1->shm.map;
   memset(data, 0, pbuf1->base.size);
#else
#ifdef USE_YUV420_BG
   pbuf1->base.stride = w1;
   pbuf1->base.size = 1920 * 1080 * 3 / 2;
   pbuf1->base.pixel_fmt = CLV_PIXEL_FMT_YUV420P;
   pbuf1->base.count_planes = 3;
   sprintf(pbuf1->base.name, "testshm%d", 0);
   clv_shm_init(&pbuf1->shm, pbuf1->base.name, pbuf1->base.size, 1);
   data = pbuf1->shm.map;
   memset(data, 0, pbuf1->base.size);
#else
#ifdef USE_RGB_BG
   pbuf1->base.stride = w1 * 4;
   pbuf1->base.size = 1920 * 1080 * 4;
   pbuf1->base.pixel_fmt = CLV_PIXEL_FMT_ARGB8888;
   pbuf1->base.count_planes = 1;
   sprintf(pbuf1->base.name, "testshm%d", 0);
   clv_shm_init(&pbuf1->shm, pbuf1->base.name, pbuf1->base.size, 1);
   data = pbuf1->shm.map;
   pixel = (u32 *)data;
   for (j = 0; j < pbuf1->base.size / 4; j++)
      pixel[j] = 0xFFFFFFFF;
#endif
#endif
#endif

   memset(pbuf2, 0, sizeof(*pbuf2));
   pbuf2->base.type = CLV_BUF_TYPE_SHM;
   pbuf2->base.w = w2;
   pbuf2->base.h = h2;
   pbuf2->base.stride = w2 * 4;
   //pbuf2->base.size = w2 * h2 * 4;
   pbuf2->base.size = 1920 * 1080 * 4;
   pbuf2->base.pixel_fmt = CLV_PIXEL_FMT_ARGB8888;
   pbuf2->base.count_planes = 1;
   sprintf(pbuf2->base.name, "testshm%d", 1);
   clv_shm_init(&pbuf2->shm, pbuf2->base.name, pbuf2->base.size, 1);
   data = pbuf2->shm.map;
   pixel = (u32 *)data;
   for (j = 0; j < pbuf2->base.size / 4; j++)
      pixel[j] = 0xFFFFFF00;

   renderer_create(c, NULL, 0, 0, x_dpy, vid);
//   set_renderer_dbg(0x33);

   c->renderer->attach_buffer(surface1, &pbuf1->base);
   c->renderer->flush_damage(surface1);
   printf("vid = %u\n", *vid);
}


/*
 * Create an RGB, double-buffered X window.
 * Return the window and context handles.
 */
static void
make_x_window(Display *x_dpy, EGLint vid,
              const char *name,
              int x, int y, int width, int height,
              Window *winRet)
{
/*
   static const EGLint attribs[] = {
      EGL_RED_SIZE, 1,
      EGL_GREEN_SIZE, 1,
      EGL_BLUE_SIZE, 1,
      EGL_DEPTH_SIZE, 1,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE
   };
   static const EGLint ctx_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
*/

   int scrnum;
   XSetWindowAttributes attr;
   unsigned long mask;
   Window root;
   Window win;
   XVisualInfo *visInfo, visTemplate;
   int num_visuals;
/*
   EGLContext ctx;
   EGLConfig config;
   EGLint num_configs;
   EGLint vid;
*/

   scrnum = DefaultScreen( x_dpy );
   root = RootWindow( x_dpy, scrnum );

/*
   if (!eglChooseConfig( egl_dpy, attribs, &config, 1, &num_configs)) {
      printf("Error: couldn't get an EGL visual config\n");
      exit(1);
   }

   assert(config);
   assert(num_configs > 0);

   if (!eglGetConfigAttrib(egl_dpy, config, EGL_NATIVE_VISUAL_ID, &vid)) {
      printf("Error: eglGetConfigAttrib() failed\n");
      exit(1);
   }
*/

   /* The X window visual must match the EGL config */
   visTemplate.visualid = vid;
   visInfo = XGetVisualInfo(x_dpy, VisualIDMask, &visTemplate, &num_visuals);
   if (!visInfo || num_visuals < 1) {
      printf("Error: couldn't get X visual\n");
      exit(1);
   }

   /* window attributes */
   attr.background_pixel = 0;
   attr.border_pixel = 0;
   attr.colormap = XCreateColormap( x_dpy, root, visInfo->visual, AllocNone);
   attr.event_mask = StructureNotifyMask | ExposureMask;
   mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

   win = XCreateWindow( x_dpy, root, 0, 0, width, height,
		        0, visInfo->depth, InputOutput,
		        visInfo->visual, mask, &attr );

   /* set hints and properties */
   {
      XSizeHints sizehints;
      sizehints.x = x;
      sizehints.y = y;
      sizehints.width  = width;
      sizehints.height = height;
      sizehints.flags = USSize | USPosition;
      XSetNormalHints(x_dpy, win, &sizehints);
      XSetStandardProperties(x_dpy, win, name, name,
                              None, (char **)NULL, 0, &sizehints);
   }

/*
   eglBindAPI(EGL_OPENGL_ES_API);

   ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attribs );
   if (!ctx) {
      printf("Error: eglCreateContext failed\n");
      exit(1);
   }

   EGLint val;
   eglQueryContext(egl_dpy, ctx, EGL_CONTEXT_CLIENT_VERSION, &val);
   assert(val == 2);

   *surfRet = eglCreateWindowSurface(egl_dpy, config, win, NULL);
   if (!*surfRet) {
      printf("Error: eglCreateWindowSurface failed\n");
      exit(1);
   }
*/

   /* sanity checks */
/*
   {
      EGLint val;
      eglQuerySurface(egl_dpy, *surfRet, EGL_WIDTH, &val);
      assert(val == width);
      eglQuerySurface(egl_dpy, *surfRet, EGL_HEIGHT, &val);
      assert(val == height);
      assert(eglGetConfigAttrib(egl_dpy, config, EGL_SURFACE_TYPE, &val));
      assert(val & EGL_WINDOW_BIT);
   }
*/

   XFree(visInfo);

   *winRet = win;
/*
   *ctxRet = ctx;
*/
}


static void
event_loop(Display *dpy, Window win, u32 w1, u32 h1, u32 w2, u32 h2, u32 win_w, u32 win_h)
{
   s32 x, y;
   s32 delta_x = 2;
   s32 delta_y = 2;

   x = y = 0;
   while (1) {
      s32 redraw = 0;
      XEvent event;

      while (XPending(dpy)) {
        XNextEvent(dpy, &event);

        switch (event.type) {
        case Expose:
           redraw = 1;
           break;
        case ConfigureNotify:
           w1 = event.xconfigure.width;
           h1 = event.xconfigure.height;
           //reshape(w, h);
           break;
        default:
           ; /*no-op*/
        }
        if (redraw) {
         //draw(x, y, w2, h2);
        }
      }
      draw(x, y, w1, h1, w2, h2);
      x += delta_x;
      y += delta_y;
      if ((x + w2) > w1) {
          delta_x = -2;
          continue;
      } else if (x < 0) {
          delta_x = 2;
          continue;
      }
      if ((y + h2) > h1) {
          delta_y = -2;
          continue;
      } else if (y < 0) {
          delta_y = 2;
          continue;
      }
//      printf("---------- %d %d %u %u %u %u\n", x, y, w, h, win_w, win_h);
   }
}

s32 main(s32 argc, char *argv[])
{
   const int winWidth = 1400, winHeight = 1000;
   Display *x_dpy;
   Window win;
   EGLint vid, vid1;
   char *dpyName = NULL;
   GLboolean printInfo = GL_FALSE;
   EGLint egl_major, egl_minor;
   int i;
   const char *s;

   x_dpy = XOpenDisplay(NULL);
   if (!x_dpy) {
      printf("Error: couldn't open display %s\n",
	     dpyName ? dpyName : getenv("DISPLAY"));
      return -1;
   }

   init(x_dpy, &win, winWidth, winHeight, winWidth, winHeight, 700, 500, &vid);
   make_x_window(x_dpy, vid,
                 "OpenGL ES 2.x tri", 0, 0, winWidth, winHeight,
                 &win);

   XMapWindow(x_dpy, win);
   g_disp.compositor.renderer->output_create(&g_disp.output, win, NULL, NULL, 0, &vid1);
   printf("vid1 = %u\n", vid1);

   reshape(winWidth, winHeight);

   event_loop(x_dpy, win, winWidth, winHeight, 700, 500, winWidth, winHeight);

   return 0;
}

