cmd_/home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/xen/.install := /bin/bash scripts/headers_install.sh /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/xen ./include/uapi/xen evtchn.h gntalloc.h gntdev.h privcmd.h; /bin/bash scripts/headers_install.sh /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/xen ./include/xen ; /bin/bash scripts/headers_install.sh /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/xen ./include/generated/uapi/xen ; for F in ; do echo "\#include <asm-generic/$$F>" > /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/xen/$$F; done; touch /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/xen/.install