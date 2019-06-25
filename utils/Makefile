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
CLOVER_UTILS_H += clover_ipc.h

CLOVER_UTILS_OBJ += clover_log.o
CLOVER_UTILS_OBJ += clover_array.o
CLOVER_UTILS_OBJ += clover_event.o
CLOVER_UTILS_OBJ += clover_region.o
CLOVER_UTILS_OBJ += clover_shm.o
CLOVER_UTILS_OBJ += clover_signal.o
CLOVER_UTILS_OBJ += clover_ipc.o

all: $(OBJ)

clover_log.o: clover_log.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clover_array.o: clover_array.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clover_event.o: clover_event.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clover_region.o: clover_region.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clover_shm.o: clover_shm.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clover_signal.o: clover_signal.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clover_ipc.o: clover_ipc.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libclover_utils.so: $(CLOVER_UTILS_OBJ)
	$(CC) -shared $^ $(LDFLAGS) -o $@

clean:
	-@rm -f $(OBJ) *.o

