#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;

struct drm_head {
	drmModeConnectorPtr connector;
	u32 connector_id;
	u32 index;
	u32 prop_crtc_id;
	u32 prop_dpms;
} heads[] = {
	{
		.connector_id = 0x57,
		.index = 0,
		.prop_crtc_id = 19,
		.prop_dpms = 2,
	},
	{
		.connector_id = 0x5E,
		.index = 1,
		.prop_crtc_id = 19,
		.prop_dpms = 2,
	},
};

struct drm_output {
	drmModeCrtcPtr crtc;
	u32 crtc_id;
	u32 index;
	u32 prop_active;
	u32 prop_mode_id;
} outputs[] = {
	{
		.crtc_id = 0x3C,
		.index = 0,
		.prop_active = 20,
		.prop_mode_id = 21,
	},
	{
		.crtc_id = 0x52,
		.index = 1,
		.prop_active = 20,
		.prop_mode_id = 21,
	},
};

struct drm_plane {
	drmModePlanePtr plane;
	u32 plane_id;
	u32 index;
	u32 prop_crtc_id;
	u32 prop_fb_id;
	u32 prop_crtc_x;
	u32 prop_crtc_y;
	u32 prop_crtc_w;
	u32 prop_crtc_h;
	u32 prop_src_x;
	u32 prop_src_y;
	u32 prop_src_w;
	u32 prop_src_h;
} planes[] = {
	{
		.plane_id = 0x36,
		.index = 0,
		.prop_src_x = 10,
		.prop_src_y = 11,
		.prop_src_w = 12,
		.prop_src_h = 13,
		.prop_crtc_x = 14,
		.prop_crtc_y = 15,
		.prop_crtc_w = 16,
		.prop_crtc_h = 17,
		.prop_fb_id = 18,
		.prop_crtc_id = 19,
	},
	{
		.plane_id = 0x39,
		.index = 1,
		.prop_src_x = 10,
		.prop_src_y = 11,
		.prop_src_w = 12,
		.prop_src_h = 13,
		.prop_crtc_x = 14,
		.prop_crtc_y = 15,
		.prop_crtc_w = 16,
		.prop_crtc_h = 17,
		.prop_fb_id = 18,
		.prop_crtc_id = 19,
	},
	{
		.plane_id = 0x3D,
		.index = 2,
		.prop_src_x = 10,
		.prop_src_y = 11,
		.prop_src_w = 12,
		.prop_src_h = 13,
		.prop_crtc_x = 14,
		.prop_crtc_y = 15,
		.prop_crtc_w = 16,
		.prop_crtc_h = 17,
		.prop_fb_id = 18,
		.prop_crtc_id = 19,
	},
	{
		.plane_id = 0x40,
		.index = 3,
		.prop_src_x = 10,
		.prop_src_y = 11,
		.prop_src_w = 12,
		.prop_src_h = 13,
		.prop_crtc_x = 14,
		.prop_crtc_y = 15,
		.prop_crtc_w = 16,
		.prop_crtc_h = 17,
		.prop_fb_id = 18,
		.prop_crtc_id = 19,
	},
	{
		.plane_id = 0x4C,
		.index = 4,
		.prop_src_x = 10,
		.prop_src_y = 11,
		.prop_src_w = 12,
		.prop_src_h = 13,
		.prop_crtc_x = 14,
		.prop_crtc_y = 15,
		.prop_crtc_w = 16,
		.prop_crtc_h = 17,
		.prop_fb_id = 18,
		.prop_crtc_id = 19,
	},
	{
		.plane_id = 0x4F,
		.index = 5,
		.prop_src_x = 10,
		.prop_src_y = 11,
		.prop_src_w = 12,
		.prop_src_h = 13,
		.prop_crtc_x = 14,
		.prop_crtc_y = 15,
		.prop_crtc_w = 16,
		.prop_crtc_h = 17,
		.prop_fb_id = 18,
		.prop_crtc_id = 19,
	},
};

void *create_dumb(s32 fd, u32 width, u32 height, u32 format, u32 *fb_id, u32 *h)
{
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_destroy_dumb destroy_arg;
	struct drm_mode_map_dumb map_arg;
	u32 handles[4] = {0, 0, 0, 0}, strides[4] = {0, 0, 0, 0}, offsets[4] = {0, 0, 0, 0};
	u32 size;
	s32 ret;
	void *map;

	memset(&create_arg, 0, sizeof create_arg);
	create_arg.bpp = 32;
	create_arg.width = width;
	create_arg.height = height;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
	if (ret)
		printf("failed to create dumb\n");

	handles[0] = create_arg.handle;
	strides[0] = create_arg.pitch;
	printf("stride = %u\n", strides[0]);
	size = create_arg.size;
	printf("%u %u\n", size, width * height *4);

	ret = drmModeAddFB2(fd, width, height, format,
			    handles, strides, offsets, fb_id,
			    0);
	if (ret < 0)
		printf("failed to add fb2 %s\n", strerror(errno));

	memset(&map_arg, 0, sizeof map_arg);
	map_arg.handle = handles[0];
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	if (ret)
		printf("failed to map dumb\n");

	map = mmap(NULL, size, PROT_WRITE,
		       MAP_SHARED, fd, map_arg.offset);
	if (map == MAP_FAILED)
		printf("failed to map dumb1\n");
	if (h)
		*h = handles[0];
	return map;
}

s32 main(s32 argc, char **argv)
{
	s32 ret;
	u32 size, cursor_size, width, height, fb_id, fb_id_cursor;
	u32 cursor_width, cursor_height;
	void *map, *cursor;
	u32 *pixel;
	drmModeAtomicReq *req;
	u32 mode_blob_id;
	u32 b, flags, format;
	u32 handle;

	width = atoi(argv[1]);
	height = atoi(argv[2]);
	size = width*height*4;
	cursor_width = atoi(argv[3]);
	cursor_height = atoi(argv[4]);
	cursor_size = cursor_width * cursor_height * 4;

	s32 fd = open("/dev/dri/card0", O_RDWR, 0644);
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

	for (s32 i = 0; i < 2; i++) {
		outputs[i].crtc = drmModeGetCrtc(fd, outputs[i].crtc_id);
		assert(outputs[i].crtc);
	}

	for (s32 i = 0; i < 2; i++) {
		heads[i].connector = drmModeGetConnector(fd, heads[i].connector_id);
		assert(heads[i].connector);
	}

	for (s32 i = 0; i < 6; i++) {
		planes[i].plane = drmModeGetPlane(fd, planes[i].plane_id);
		assert(planes[i].plane);
	}

	map = create_dumb(fd, width, height, DRM_FORMAT_XRGB8888, &fb_id, NULL);
	printf("map = %p, fb_id = %u\n", map, fb_id);

	if (cursor_width) {
	cursor = create_dumb(fd, cursor_width, cursor_height, DRM_FORMAT_XRGB8888, &fb_id_cursor, &handle);
	printf("cursor = %p, fb_id_cursor = %u\n", cursor, fb_id_cursor);
	}

	if (cursor_width) {
		u32 *p = (u32 *)cursor;
		for (s32 i = 0; i < cursor_height / 4; i++) {
			for (s32 j = 0; j < cursor_width/4; j++) {
				*(p + i * cursor_width + j) = 0xFFFF0000;
			}
			for (s32 j = cursor_width/4; j < cursor_width / 2; j++) {
				*(p + i * cursor_width + j) = 0xFF00FFFF;
			}
			for (s32 j = cursor_width/2; j < cursor_width / 4 * 3; j++) {
				*(p + i * cursor_width + j) = 0xFF0000FF;
			}
			for (s32 j = cursor_width / 4 * 3; j < cursor_width; j++) {
				*(p + i * cursor_width + j) = 0xFFFFFF00;
			}
		}
		for (s32 i = cursor_height / 4; i < cursor_height / 2; i++) {
			for (s32 j = 0; j < cursor_width/4; j++) {
				*(p + i * cursor_width + j) = 0xFF00FFFF;
			}
			for (s32 j = cursor_width/4; j < cursor_width / 2; j++) {
				*(p + i * cursor_width + j) = 0xFF0000FF;
			}
			for (s32 j = cursor_width/2; j < cursor_width / 4 * 3; j++) {
				*(p + i * cursor_width + j) = 0xFFFFFF00;
			}
			for (s32 j = cursor_width / 4 * 3; j < cursor_width; j++) {
				*(p + i * cursor_width + j) = 0xFFFF0000;
			}
		}
		for (s32 i = cursor_height / 2; i < cursor_height / 4 * 3; i++) {
			for (s32 j = 0; j < cursor_width/4; j++) {
				*(p + i * cursor_width + j) = 0xFF0000FF;
			}
			for (s32 j = cursor_width/4; j < cursor_width / 2; j++) {
				*(p + i * cursor_width + j) = 0xFFFFFF00;
			}
			for (s32 j = cursor_width/2; j < cursor_width / 4 * 3; j++) {
				*(p + i * cursor_width + j) = 0xFFFF0000;
			}
			for (s32 j = cursor_width / 4 * 3; j < cursor_width; j++) {
				*(p + i * cursor_width + j) = 0xFF00FFFF;
			}
		}
		for (s32 i = cursor_height / 4 * 3; i < cursor_height; i++) {
			for (s32 j = 0; j < cursor_width/4; j++) {
				*(p + i * cursor_width + j) = 0xFFFFFF00;
			}
			for (s32 j = cursor_width/4; j < cursor_width / 2; j++) {
				*(p + i * cursor_width + j) = 0xFFFF0000;
			}
			for (s32 j = cursor_width/2; j < cursor_width / 4 * 3; j++) {
				*(p + i * cursor_width + j) = 0xFF00FFFF;
			}
			for (s32 j = cursor_width / 4 * 3; j < cursor_width; j++) {
				*(p + i * cursor_width + j) = 0xFF0000FF;
			}
		}
	}

	req = drmModeAtomicAlloc();
	ret = drmModeCreatePropertyBlob(fd,
					&heads[0].connector->modes[0],
					sizeof(heads[0].connector->modes[0]),
					&mode_blob_id);
	if (ret != 0)
		printf("failed to create mode property blob: %s\n",
			   strerror(errno));

loop:
	pixel = (u32 *)map;
	for (s32 i = 0; i < size / 4; i++) {
		pixel[i] = 0xFF00FF00;
	}
	flags = 0;
//	flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	drmModeAtomicAddProperty(req, outputs[0].crtc_id,
				outputs[0].prop_mode_id,
				mode_blob_id);
	drmModeAtomicAddProperty(req, outputs[0].crtc_id,
				outputs[0].prop_active,
				1);
	flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

	drmModeAtomicAddProperty(req, heads[0].connector_id,
				heads[0].prop_crtc_id,
				outputs[0].crtc_id);

/////////////////////////////////////
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_crtc_id,
				outputs[0].crtc_id);

	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_fb_id,
				fb_id);

	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_crtc_x,
				0);
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_crtc_y,
				0);
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_crtc_w,
				heads[0].connector->modes[0].hdisplay);
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_crtc_h,
				heads[0].connector->modes[0].vdisplay);
	
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_src_x,
				0);
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_src_y,
				0);
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_src_w,
				width << 16);
	drmModeAtomicAddProperty(req, planes[0].plane_id,
				planes[0].prop_src_h,
				height << 16);

	if (1) {
/////////////////////////////////////
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_crtc_id,
				outputs[0].crtc_id);

	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_fb_id,
				fb_id_cursor);

	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_crtc_x,
				0);
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_crtc_y,
				0);
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_crtc_w,
				cursor_width);
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_crtc_h,
				cursor_height);
	
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_src_x,
				0);
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_src_y,
				0);
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_src_w,
				cursor_width << 16);
	drmModeAtomicAddProperty(req, planes[1].plane_id,
				planes[1].prop_src_h,
				cursor_height << 16);
	} else {
		//drmModeSetCursor(fd, outputs[0].crtc_id, handle, cursor_width, cursor_height);
	}

	ret = drmModeAtomicCommit(fd, req, flags, &b);
	if (ret)
		printf("Commit failed.\n");
	ret = drmModeAtomicCommit(fd, req, flags, &b);
	if (ret)
		printf("Commit failed.");

	sleep(10);


	flags = 0;
	pixel = (u32 *)map;
	for (s32 i = 0; i < size / 4; i++) {
		pixel[i] = 0x80123456;
	}
	ret = drmModeAtomicCommit(fd, req, flags, &b);
	if (ret)
		printf("Commit failed.");

	sleep(10);

	flags = 0;
	drmModeAtomicAddProperty(req, outputs[0].crtc_id,
				outputs[0].prop_mode_id,
				0);
	drmModeAtomicAddProperty(req, outputs[0].crtc_id,
				outputs[0].prop_active,
				0);
	flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

	drmModeAtomicAddProperty(req, heads[0].connector_id,
				heads[0].prop_crtc_id,
				0);
	printf("inactive\n");
	ret = drmModeAtomicCommit(fd, req, flags, &b);
	if (ret)
		printf("Commit failed.");

	sleep(40);

	goto loop;

	drmModeAtomicFree(req);

	drmModeDestroyPropertyBlob(fd, mode_blob_id);
	for (s32 i = 0; i < 2; i++) {
		drmModeFreeCrtc(outputs[i].crtc);
	}

	for (s32 i = 0; i < 2; i++) {
		drmModeFreeConnector(heads[i].connector);
	}

	for (s32 i = 0; i < 6; i++) {
		drmModeFreePlane(planes[i].plane);
	}
	close(fd);
	return 0;
}


