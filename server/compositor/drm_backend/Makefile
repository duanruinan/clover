include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJ := libclover_drm_backend.so test_drm_backend

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I$(RPATH)/server/compositor
CFLAGS += -fPIC

CLOVER_UTILS_H += $(RPATH)/utils/clover_utils.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_log.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_array.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_event.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_region.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_shm.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_signal.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_ipc.h

PLATFORM_CFLAGS += -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm
PLATFORM_LDFLAGS += -ldrm -lgbm -lrt -ludev

all: $(OBJ)

clean:
	-@rm -f libclover_drm_backend.so
	-@rm -f *.o
	-@rm -f test_drm_backend

libclover_drm_backend.so: drm_backend.o
	$(CC) -shared -rdynamic $^ $(LDFLAGS) $(PLATFORM_LDFLAGS) \
		-L$(RPATH)/server/renderer -lclover_renderer \
		-L$(RPATH)/server/compositor -lclover_compositor -o $@

drm_backend.o: drm_backend.c \
		$(CLOVER_UTILS_H) $(RPATH)/server/compositor/compositor.h
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

test_drm_backend: test_drm_backend.o libclover_drm_backend.so
	$(CC) $< $(LDFLAGS) \
		-L$(RPATH)/server/compositor/drm_backend -lclover_drm_backend \
		-L$(RPATH)/utils -lclover_utils \
		-L$(RPATH)/server/renderer -lclover_renderer \
		-L$(RPATH)/server/compositor -lclover_compositor \
		-o $@

test_drm_backend.o: test_drm_backend.c \
		$(CLOVER_UTILS_H) $(RPATH)/server/compositor/compositor.h
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

