include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean
.PHONY: main
.PHONY: main-clean
.PHONY: gl-backend
.PHONY: gl-backend-clean

OBJ := main gl-backend
OBJ-CLEAN := main-clean gl-backend-clean

all: $(OBJ)
clean: $(OBJ-CLEAN)

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I$(RPATH)/server/compositor
CFLAGS += -fPIC

main: libclover_renderer.so

libclover_renderer.so: renderer.o
	$(CC) -shared $^ $(LDFLAGS) -ldl -o $@

renderer.o: renderer.c \
		$(RPATH)/server/compositor/clover_compositor.h \
		$(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

main-clean:
	-@rm -f *.o
	-@rm -f libclover_renderer.so

gl-backend:
	make -C $(RPATH)/server/renderer/gl_renderer PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)

gl-backend-clean:
	make -C $(RPATH)/server/renderer/gl_renderer PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG) clean

