include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

ifeq ($(PLATFORM), RK3399)
OBJ := libclover_gl_renderer.so test_gl_renderer_drm
else
OBJ := libclover_gl_renderer.so test_gl_renderer test_dma_buf
endif

PLATFORM_CFLAGS += -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm
PLATFORM_LDFLAGS += -lGLESv2 -lEGL

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

all: $(OBJ)

gl_renderer.o: gl_renderer.c $(RPATH)/server/compositor/compositor.h \
		$(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libclover_gl_renderer.so: gl_renderer.o
	$(CC) -shared -rdynamic $^ $(LDFLAGS) $(PLATFORM_LDFLAGS) -o $@

test_gl_renderer: test_gl_renderer.o ../libclover_renderer.so
	$(CC) $< $(LDFLAGS) -lX11 \
		-L$(RPATH)/server/renderer -lclover_renderer -o $@

test_gl_renderer.o: test_gl_renderer.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

test_gl_renderer_drm: test_gl_renderer_drm.o ../libclover_renderer.so
	$(CC) $< $(LDFLAGS) -ldrm -lgbm -lpthread \
		-L$(RPATH)/server/renderer -lclover_renderer -o $@

test_gl_renderer_drm.o: test_gl_renderer_drm.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

test_dma_buf: test_dma_buf.o ../libclover_renderer.so
	$(CC) $< -L$(RPATH)/utils/ -lclover_utils -ldrm -lgbm \
		-lEGL -lGLESv2 \
		-lX11 -L$(RPATH)/server/renderer -lclover_renderer -o $@

test_dma_buf.o: test_dma_buf.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJ) *.o
	-@rm -f test_gl_renderer_drm
	-@rm -f test_gl_renderer
	-@rm -f test_dma_buf

