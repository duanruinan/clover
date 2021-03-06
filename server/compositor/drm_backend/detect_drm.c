#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

typedef int32_t s32;
typedef uint32_t u32;

static s32 get_prop_value(s32 fd, drmModeObjectProperties *props,
			  const char *name, u32 *value)
{
	drmModePropertyPtr property;
	u32 i;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!property)
			continue;
		if (!strcasecmp(property->name, name)) {
			*value = props->prop_values[i];
			drmModeFreeProperty(property);
			return 0;
		}
		drmModeFreeProperty(property);
	}
	return -ENOENT;
}

static s32 get_prop_id(s32 fd, drmModeObjectProperties *props, const char *name)
{
	drmModePropertyPtr property;
	u32 i, id = 0;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!property)
			continue;
		if (!strcasecmp(property->name, name))
			id = property->prop_id;
		drmModeFreeProperty(property);
		if (id)
			return id;
	}

	return -1;
}

s32 main(s32 argc, char **argv)
{
	drmModeRes *res;
	drmModePlaneRes *pres;
	drmModeConnectorPtr conn;
	drmModeCrtcPtr crtc;
	drmModeEncoderPtr enc;
	drmModePlanePtr plane;
	u32 possible_crtcs;
	drmModeObjectProperties *props;
	s32 fd;
	s32 i, j;
	u32 value;
	u32 zpos;
	u32 color_space;

	fd = open("/dev/dri/card0", O_RDWR, 0644);
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	res = drmModeGetResources(fd);
	printf("Dump connectors\n");
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;
		props = drmModeObjectGetProperties(fd,
						res->connectors[i],
						DRM_MODE_OBJECT_CONNECTOR);
		printf("\tIndex: %u, Connecor ID: 0x%08X:\n", i,
		       res->connectors[i]);
		printf("\t\tCRTC_ID = %u\n", get_prop_id(fd, props, "CRTC_ID"));
		printf("\t\tDPMS = %u\n", get_prop_id(fd, props, "DPMS"));
		drmModeFreeObjectProperties(props);
		printf("\t\tPossible Encoders:\n");
		for (j = 0; j < conn->count_encoders; j++)
			printf("\t\t\tEncoder 0x%08X\n", conn->encoders[j]);
		drmModeFreeConnector(conn);
	}

	printf("Dump encoders\n");
	for (i = 0; i < res->count_encoders; i++) {
		enc = drmModeGetEncoder(fd, res->encoders[i]);
		if (!enc)
			continue;
		printf("\tIndex: %u, Encoder ID: 0x%08X:\n", i,
		       res->encoders[i]);
		printf("\t\tPossible CRTCs:\n");
		possible_crtcs = enc->possible_crtcs;
		for (j = 0; j < sizeof(u32) * 8; j++) {
			if (possible_crtcs & (1 << j))
				printf("\t\t\tCRTC pipe %u, ID: 0x%08X\n",
				       j, res->crtcs[j]);
		}
		drmModeFreeEncoder(enc);
	}

	printf("Dump CRTCs\n");
	for (i = 0; i < res->count_crtcs; i++) {
		crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (!crtc)
			continue;
		printf("\tIndex: %u, CRTC ID: 0x%08X:\n", i, res->crtcs[i]);
		props = drmModeObjectGetProperties(fd,
						res->crtcs[i],
						DRM_MODE_OBJECT_CRTC);
		printf("\t\tACTIVE = %u\n", get_prop_id(fd, props, "ACTIVE"));
		printf("\t\tMODE_ID = %u\n", get_prop_id(fd, props, "MODE_ID"));
		drmModeFreeObjectProperties(props);
		drmModeFreeCrtc(crtc);
	}

	pres = drmModeGetPlaneResources(fd);
	printf("Dump Planes\n");
	for (i = 0; i < pres->count_planes; i++) {
		plane = drmModeGetPlane(fd, pres->planes[i]);
		if (!plane)
			continue;
		printf("\tIndex: %u, Plane ID: 0x%08X:\n", i, pres->planes[i]);
		props = drmModeObjectGetProperties(fd,
						pres->planes[i],
						DRM_MODE_OBJECT_PLANE);
		get_prop_value(fd, props, "type", &value);
		printf("\t\tCRTC_ID = %u\n", get_prop_id(fd, props, "CRTC_ID"));
		printf("\t\tFB_ID = %u\n", get_prop_id(fd, props, "FB_ID"));
		printf("\t\tCRTC_X = %u\n", get_prop_id(fd, props, "CRTC_X"));
		printf("\t\tCRTC_Y = %u\n", get_prop_id(fd, props, "CRTC_Y"));
		printf("\t\tCRTC_W = %u\n", get_prop_id(fd, props, "CRTC_W"));
		printf("\t\tCRTC_H = %u\n", get_prop_id(fd, props, "CRTC_H"));
		printf("\t\tSRC_X = %u\n", get_prop_id(fd, props, "SRC_X"));
		printf("\t\tSRC_Y = %u\n", get_prop_id(fd, props, "SRC_Y"));
		printf("\t\tSRC_W = %u\n", get_prop_id(fd, props, "SRC_W"));
		printf("\t\tSRC_H = %u\n", get_prop_id(fd, props, "SRC_H"));
		get_prop_value(fd, props, "COLOR_SPACE", &color_space);
		printf("\t\tCOLOR_SPACE = %d\n", color_space);
		get_prop_value(fd, props, "ZPOS", &zpos);
		printf("\t\tZPOS = %d\n", zpos);
		if (value == DRM_PLANE_TYPE_CURSOR)
			printf("\t\tPlane type: Cursor\n");
		else if (value == DRM_PLANE_TYPE_PRIMARY)
			printf("\t\tPlane type: Primary\n");
		else if (value == DRM_PLANE_TYPE_OVERLAY)
			printf("\t\tPlane type: Overlay\n");
		drmModeFreeObjectProperties(props);
		printf("\t\tPossible CRTC:\n");
		possible_crtcs = plane->possible_crtcs;
		for (j = 0; j < sizeof(u32) * 8; j++) {
			if (possible_crtcs & (1 << j))
				printf("\t\t\tCRTC pipe %u, ID: 0x%08X\n",
				       j, res->crtcs[j]);
		}
		printf("\t\tPlane formats\n");
		for (j = 0; j < plane->count_formats; j++) {
			printf("\t\t\t%4.4s\n", (char *)&plane->formats[j]);
		}
		drmModeFreePlane(plane);
	}
	drmModeFreeResources(res);
	drmModeFreePlaneResources(pres);
	close(fd);
	return 0;
}

