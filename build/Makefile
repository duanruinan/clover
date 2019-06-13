include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

all:
	make -C $(RPATH)/utils RPATH=$(RPATH) PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)
	make -C $(RPATH)/server RPATH=$(RPATH) PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)

clean:
	make -C $(RPATH)/utils RPATH=$(RPATH) clean
	make -C $(RPATH)/server RPATH=$(RPATH) clean

