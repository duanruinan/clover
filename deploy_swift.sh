#!/bin/sh

echo $# $1
if [ "$#" != "1" ]; then
	exit 0;
fi

echo "deploying..."
cp utils/libclover_utils.so $1/src/swift/clover/lib
cp utils/*.h $1/src/swift/clover/include

if [ -d "$1/out/for_deploy" ]; then
	echo "deploy clover"
	cp server/clover_server $1/out/for_deploy/usr/bin
	cp client/clover_simple_client $1/out/for_deploy/usr/bin
	cp client/clover_shell $1/out/for_deploy/usr/bin
	cp client/clover_input $1/out/for_deploy/usr/bin
	cp utils/libclover_utils.so $1/out/for_deploy/usr/lib
	cp server/compositor/libclover_compositor.so $1/out/for_deploy/usr/lib
	cp server/compositor/drm_backend/libclover_drm_backend.so $1/out/for_deploy/usr/lib
	cp server/renderer/libclover_renderer.so $1/out/for_deploy/usr/lib
	cp server/renderer/gl_renderer/libclover_gl_renderer.so $1/out/for_deploy/usr/lib
	rm -rf $1/out/for_deploy/etc
	mkdir -p $1/out/for_deploy/etc
	cp clover_extended.xml $1/out/for_deploy/etc
fi

