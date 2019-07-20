include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean
.PHONY: renderer
.PHONY: main
.PHONY: main-clean
.PHONY: renderer-clean
.PHONY: compositor
.PHONY: compositor-clean

OBJ := renderer compositor main
OBJ-CLEAN := renderer-clean compositor-clean main-clean

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I$(RPATH)/server/compositor
CFLAGS += -fPIC
LDFLAGS += -L$(RPATH)/utils -lclover_utils
LDFLAGS += -L$(RPATH)/server/compositor -lclover_compositor
LDFLAGS += -L$(RPATH)/server/renderer -lclover_renderer

CLOVER_UTILS_H += $(RPATH)/utils/clover_utils.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_log.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_array.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_event.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_region.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_shm.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_signal.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_ipc.h
CLOVER_UTILS_H += $(RPATH)/utils/clover_protocal.h

all: $(OBJ)

clean: $(OBJ-CLEAN)

renderer:
	make -C $(RPATH)/server/renderer PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)

renderer-clean:
	make -C $(RPATH)/server/renderer PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG) clean

compositor:
	make -C $(RPATH)/server/compositor PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)

compositor-clean:
	make -C $(RPATH)/server/compositor PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG) clean

main: clover_server

main-clean:
	-@rm -f *.o
	-@rm -f clover_server

clover_server: main.o \
		$(RPATH)/utils/libclover_utils.so \
		$(RPATH)/server/compositor/libclover_compositor.so \
		$(RPATH)/server/renderer/libclover_renderer.so
	$(CC) $< $(LDFLAGS) -o $@

main.o: main.c $(RPATH)/server/compositor/clover_compositor.h \
		$(CLOVER_UTILS_H)
	$(CC) -c $< -I. $(CFLAGS) -o $@

