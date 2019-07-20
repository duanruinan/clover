include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I$(RPATH)/server/compositor
CFLAGS += -fPIC
LDFLAGS += -L$(RPATH)/utils -lclover_utils

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
PLATFORM_LDFLAGS += -lgbm -lEGL -lGLESv2

all: clover_simple_client

clover_simple_client: main.o \
		$(RPATH)/utils/libclover_utils.so
	$(CC) $< $(LDFLAGS) $(PLATFORM_LDFLAGS) -o $@

main.o: main.c $(RPATH)/server/compositor/clover_compositor.h \
		$(CLOVER_UTILS_H)
	$(CC) -c $< -I. $(CFLAGS) $(PLATFORM_CFLAGS) -o $@

clean:
	-@rm -f *.o
	-@rm -f clover_simple_client

