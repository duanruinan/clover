cmd_/home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/uapi/.install := /bin/bash scripts/headers_install.sh /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/uapi ./include/uapi ; /bin/bash scripts/headers_install.sh /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/uapi ./include ; /bin/bash scripts/headers_install.sh /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/uapi ./include/generated/uapi ; for F in ; do echo "\#include <asm-generic/$$F>" > /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/uapi/$$F; done; touch /home/users/liuzhongzhi/rk3399_linux/buildroot/output/host/usr/aarch64-rockchip-linux-gnu/sysroot/usr/include/uapi/.install