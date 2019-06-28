include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean
.PHONY: main
.PHONY: main-clean
.PHONY: drm-backend
.PHONY: drm-backend-clean

OBJ := main drm-backend
OBJ-CLEAN := drm-backend-clean main-clean

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

all: $(OBJ)

clean: $(OBJ-CLEAN)

drm-backend:
	make -C $(RPATH)/server/compositor/drm_backend PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)

drm-backend-clean:
	make -C $(RPATH)/server/compositor/drm_backend PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG) clean

main: libclover_compositor.so

libclover_compositor.so: compositor.o
	$(CC) -shared $^ $(LDFLAGS) -ldl -o $@

compositor.o: compositor.c \
		$(RPATH)/server/compositor/compositor.h \
		$(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

main-clean:
	-@rm -f compositor.o
	-@rm -f libclover_compositor.so

