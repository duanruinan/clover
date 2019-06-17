include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJ := libclover_utils.so

CFLAGS += -I$(RPATH)/utils
CFLAGS += -fPIC

CLOVER_UTILS_H += clover_utils.h
CLOVER_UTILS_H += clover_log.h
CLOVER_UTILS_H += clover_array.h
CLOVER_UTILS_H += clover_event.h
CLOVER_UTILS_H += clover_region.h
CLOVER_UTILS_H += clover_shm.h
CLOVER_UTILS_H += clover_signal.h

CLOVER_UTILS_OBJ += clover_log.o
CLOVER_UTILS_OBJ += clover_array.o
CLOVER_UTILS_OBJ += clover_event.o
CLOVER_UTILS_OBJ += clover_region.o
CLOVER_UTILS_OBJ += clover_shm.o
CLOVER_UTILS_OBJ += clover_signal.o

all: $(OBJ)

clover_log.o: clover_log.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libclover_utils.so: $(CLOVER_UTILS_OBJ)
	$(CC) -shared $^ $(LDFLAGS) -o $@

clean:
	-@rm -f $(OBJ) *.o

