#!/bin/sh

adb push server/clover_server /tmp/
adb push server/compositor/drm_backend/detect_drm /tmp
adb push client/clover_simple_client /tmp/
adb push utils/libclover_utils.so /tmp/
adb push server/compositor/libclover_compositor.so /tmp/
adb push server/compositor/drm_backend/libclover_drm_backend.so /tmp/
adb push server/renderer/libclover_renderer.so /tmp/
adb push server/renderer/gl_renderer/libclover_gl_renderer.so /tmp/
adb push clover_extended.xml /tmp/
adb push clover_duplicated.xml /tmp/
adb push set_rk3399_env /tmp/
