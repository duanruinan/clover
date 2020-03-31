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
	drmModeConnectorPtr conn;
	drmModeObjectProperties *props;
	drmModeModeInfoPtr m;
	drmModeModeInfo n;
	s32 fd;
	s32 i, j, ret;
	u32 value;

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
		m = conn->modes;
		for (j = 0; j < conn->count_modes; j++) {
			printf("%u | %u %u %u %u %u | %u %u %u %u %u | %u | %u"
				" %u %s\n",
				m->clock,
				m->hdisplay, m->hsync_start, m->hsync_end, m->htotal, m->hskew,
				m->vdisplay, m->vsync_start, m->vsync_end, m->vtotal, m->vscan,
				m->vrefresh, m->flags, m->type, m->name);
			m++;
		}
		memset(&n, 0, sizeof(n));
		n.clock = 592000;
		n.hdisplay = 2560;
		n.hsync_start = 2568;
		n.hsync_end = 2600;
		n.htotal = 2666;
		n.hskew = 0;
		n.vdisplay = 1440;
		n.vsync_start = 1465;
		n.vsync_end = 1473;
		n.vtotal = 1543;
		n.vscan = 0;
		n.vrefresh = 144;
		n.flags = 9;
		n.type = 64;
		strcpy(n.name, "Test");
		printf("attach\n");
		ret = drmModeAttachMode(fd, res->connectors[i], &n);
		if (!ret) {
			printf("Attach successful\n");
		}
		m = conn->modes;
		for (j = 0; j < conn->count_modes; j++) {
			printf("%u | %u %u %u %u %u | %u %u %u %u %u | %u | %u"
				" %u %s\n",
				m->clock,
				m->hdisplay, m->hsync_start, m->hsync_end, m->htotal, m->hskew,
				m->vdisplay, m->vsync_start, m->vsync_end, m->vtotal, m->vscan,
				m->vrefresh, m->flags, m->type, m->name);
			m++;
		}
		while (1) {
			sleep(1);	
		}
		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
	close(fd);
	return 0;
}

