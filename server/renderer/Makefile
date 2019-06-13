include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

all:
	make -C $(RPATH)/server/renderer/gl_renderer PLATFORM=$(PLATFORM) \
		CLV_DEBUG=$(CLV_DEBUG)

clean:
	make -C $(RPATH)/server/renderer/gl_renderer RPATH=$(RPATH) clean

