include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJ := libclover_gl_renderer.so test_gl_renderer

ifeq ($(PLATFORM), RK3399)
PLATFORM_LDFLAGS += -lGLESv2 -lEGL
else
PLATFORM_LDFLAGS += -lGLESv2 -lEGL
endif

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

all: $(OBJ)

gl_renderer.o: gl_renderer.c $(RPATH)/server/compositor/compositor.h \
		$(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libclover_gl_renderer.so: gl_renderer.o
	$(CC) -shared $^ $(LDFLAGS) -o $@

test_gl_renderer: test_gl_renderer.o libclover_gl_renderer.so
	$(CC) $^ $(LDFLAGS) $(PLATFORM_LDFLAGS) -lX11 -o $@

test_gl_renderer.o: test_gl_renderer.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(PLATFORM_CFLAGS) $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJ) *.o

