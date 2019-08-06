include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJ := libclover_drm_backend.so detect_drm test_drm_backend drm_atomic_test \
	drm_atomic_test_nv24

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
CLOVER_UTILS_H += $(RPATH)/utils/clover_protocal.h

PLATFORM_CFLAGS += -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm
PLATFORM_LDFLAGS += -ldrm -lgbm -lrt -ludev

all: $(OBJ)

clean:
	-@rm -f libclover_drm_backend.so
	-@rm -f *.o
	-@rm detect_drm
	-@rm test_drm_backend
	-@rm drm_atomic_test
	-@rm drm_atomic_test_nv24

libclover_drm_backend.so: drm_backend.o
	$(CC) -shared -rdynamic $^ $(LDFLAGS) $(PLATFORM_LDFLAGS) \
		-L$(RPATH)/server/renderer -lclover_renderer \
		-L$(RPATH)/server/compositor -lclover_compositor -o $@

drm_backend.o: drm_backend.c \
		$(CLOVER_UTILS_H) $(RPATH)/server/compositor/clover_compositor.h
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

detect_drm: detect_drm.o
	$(CC) $< $(PLATFORM_LDFLAGS) -o $@

detect_drm.o: detect_drm.c
	$(CC) -c $< $(PLATFORM_CFLAGS) -o $@

test_drm_backend: test_drm_backend.o
	$(CC) $< $(PLATFORM_LDFLAGS) -L$(RPATH)/utils -lclover_utils -o $@

test_drm_backend.o: test_drm_backend.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

drm_atomic_test: drm_atomic_test.o
	$(CC) $< $(PLATFORM_LDFLAGS) -o $@

drm_atomic_test.o: drm_atomic_test.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

drm_atomic_test_nv24: drm_atomic_test_nv24.o
	$(CC) $< $(PLATFORM_LDFLAGS) -o $@

drm_atomic_test_nv24.o: drm_atomic_test_nv24.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

