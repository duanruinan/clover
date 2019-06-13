include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJ := libclover_utils.so

CFLAGS += -I$(RPATH)/utils
CFLAGS += -fPIC

CLOVER_UTILS_H += clover_utils.h
CLOVER_UTILS_H += clover_log.h

CLOVER_UTILS_OBJ += clover_log.o

all: $(OBJ)

clover_log.o: clover_log.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libclover_utils.so: $(CLOVER_UTILS_OBJ)
	$(CC) -shared $^ $(LDFLAGS) -o $@

clean:
	-@rm -f $(OBJ) *.o

