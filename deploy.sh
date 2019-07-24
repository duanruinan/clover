#!/bin/sh

echo $# $1
if [ "$#" != "1" ]; then
	exit 0;
fi

echo "deploying..."
cp server/clover_server $1/externals/bin/aarch64-linux/
cp client/clover_simple_client $1/externals/bin/aarch64-linux/
cp client/clover_shell $1/externals/bin/aarch64-linux/
cp utils/libclover_utils.so $1/externals/lib/aarch64-linux/
cp server/compositor/libclover_compositor.so $1/externals/lib/aarch64-linux/
cp server/compositor/drm_backend/libclover_drm_backend.so $1/externals/lib/aarch64-linux/
cp server/renderer/libclover_renderer.so $1/externals/lib/aarch64-linux/
cp server/renderer/gl_renderer/libclover_gl_renderer.so $1/externals/lib/aarch64-linux/
rm -f $1/externals/etc/aarch64-linux/clover_config.xml
cp clover_extended.xml $1/externals/etc/aarch64-linux/clover_config.xml

