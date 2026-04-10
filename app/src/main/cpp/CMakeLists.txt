#
# GStreamer RTP/H.265 Player — Native JNI Bridge
#
# This file is loaded by the Kotlin layer via System.loadLibrary("gstreamer-player").
# It exposes a minimal JNI interface for GStreamer pipeline lifecycle:
#   - nativeInit()
#   - nativeStart(surface: Surface)
#   - nativeStop()
#   - nativeSetPipeline(pipeline: String)
#

cmake_minimum_required(VERSION 3.22.1)
project("gstreamer-player")

# ── GStreamer Android SDK ──────────────────────────────────────────────
# The GSTREAMER_SDK_DIR must be set in local.properties or passed via
# -DGSTREAMER_SDK_DIR=<path> from Gradle.
#
# Expected layout inside the SDK directory:
#   <GSTREAMER_SDK_DIR>/include/gstreamer-1.0/
#   <GSTREAMER_SDK_DIR>/include/glib-2.0/
#   <GSTREAMER_SDK_DIR>/lib/glib-2.0/include/
#   <GSTREAMER_SDK_DIR>/lib/gstreamer-1.0/
#   <GSTREAMER_SDK_DIR>/lib/libgstreamer-1.0.so
#   <GSTREAMER_SDK_DIR>/lib/libglib-2.0.so
#   ...

if(NOT DEFINED GSTREAMER_SDK_DIR)
    message(FATAL_ERROR
        "GSTREAMER_SDK_DIR is not defined. "
        "Set it in local.properties as gstreamer.sdk.dir=<path>"
    )
endif()

# Determine ABI subdirectory based on the current build target
if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(GST_ABI_DIR "arm64")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(GST_ABI_DIR "armv7")
elseif(${ANDROID_ABI} STREQUAL "x86_64")
    set(GST_ABI_DIR "x86_64")
elseif(${ANDROID_ABI} STREQUAL "x86")
    set(GST_ABI_DIR "x86")
else()
    set(GST_ABI_DIR ${ANDROID_ABI})
endif()

# ── Paths ──────────────────────────────────────────────────────────────
set(GST_ROOT_DIR        ${GSTREAMER_SDK_DIR}/${GST_ABI_DIR})
set(GST_INCLUDE         ${GST_ROOT_DIR}/include/gstreamer-1.0)
set(GLIB_INCLUDE        ${GST_ROOT_DIR}/include/glib-2.0)
set(GLIB_CONFIG_INCLUDE ${GST_ROOT_DIR}/lib/glib-2.0/include)
set(GST_LIB_DIR         ${GST_ROOT_DIR}/lib)

# ── JNI ────────────────────────────────────────────────────────────────
# In Android NDK, jni.h and other JNI headers are provided by the system
# and are already in the default include path.

include_directories(
    ${GST_INCLUDE}
    ${GLIB_INCLUDE}
    ${GLIB_CONFIG_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Source files ───────────────────────────────────────────────────────
set(NATIVE_SOURCES
    gstreamer_android_bootstrap.c
    gstreamer-jni.cpp
    gst-player.cpp
)

# ── Shared library ─────────────────────────────────────────────────────
add_library(gstreamer-player SHARED ${NATIVE_SOURCES})

# ── Link GStreamer libraries ──────────────────────────────────────────
# Note: Linking against static libraries (.a).
# We use --start-group/--end-group because GStreamer and FFmpeg static
# libraries have many circular dependencies.
target_link_libraries(
    gstreamer-player

    -Wl,--start-group
    # 1. GStreamer Plugins (Static)
    -Wl,--whole-archive
    ${GST_LIB_DIR}/gstreamer-1.0/libgstcoreelements.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstudp.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstrtp.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstrtpmanager.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstvideoparsersbad.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstisomp4.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstandroidmedia.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstopengl.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstlibav.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstvideoconvertscale.a
    ${GST_LIB_DIR}/gstreamer-1.0/libgstplayback.a
    -Wl,--no-whole-archive

    # 2. GStreamer Helper Libraries
    ${GST_LIB_DIR}/libgstpbutils-1.0.a
    ${GST_LIB_DIR}/libgstvideo-1.0.a
    ${GST_LIB_DIR}/libgstaudio-1.0.a
    ${GST_LIB_DIR}/libgsttag-1.0.a
    ${GST_LIB_DIR}/libgstrtp-1.0.a
    ${GST_LIB_DIR}/libgstallocators-1.0.a
    ${GST_LIB_DIR}/libgstcodecparsers-1.0.a
    ${GST_LIB_DIR}/libgstnet-1.0.a
    ${GST_LIB_DIR}/libgstbase-1.0.a
    ${GST_LIB_DIR}/libgstcontroller-1.0.a
    ${GST_LIB_DIR}/libgstgl-1.0.a
    ${GST_LIB_DIR}/libgstriff-1.0.a
    ${GST_LIB_DIR}/libgstphotography-1.0.a
    ${GST_LIB_DIR}/libgraphene-1.0.a
    ${GST_LIB_DIR}/libpng16.a
    ${GST_LIB_DIR}/libjpeg.a
    ${GST_LIB_DIR}/libavcodec.a
    ${GST_LIB_DIR}/libavfilter.a
    ${GST_LIB_DIR}/libavformat.a
    ${GST_LIB_DIR}/libavutil.a
    ${GST_LIB_DIR}/libswscale.a
    ${GST_LIB_DIR}/libswresample.a

    # 3. GStreamer Core
    ${GST_LIB_DIR}/libgstreamer-1.0.a

    # 4. GLib Stack
    ${GST_LIB_DIR}/libgio-2.0.a
    ${GST_LIB_DIR}/libgobject-2.0.a
    ${GST_LIB_DIR}/libgmodule-2.0.a
    ${GST_LIB_DIR}/libglib-2.0.a

    # 5. Transitive / Low-level dependencies
    ${GST_LIB_DIR}/libintl.a
    ${GST_LIB_DIR}/libffi.a
    ${GST_LIB_DIR}/libpcre2-8.a
    ${GST_LIB_DIR}/libiconv.a
    ${GST_LIB_DIR}/liborc-0.4.a
    ${GST_LIB_DIR}/libz.a
    ${GST_LIB_DIR}/libbz2.a
    ${GST_LIB_DIR}/libxml2.a
    ${GST_LIB_DIR}/libssl.a
    ${GST_LIB_DIR}/libcrypto.a
    -Wl,--end-group

    # 7. Android Native
    android
    log
    EGL
    GLESv2
    atomic
    m
)

# Force 4KB page size.
# We no longer use -fuse-ld=bfd or -Wl,-z,notext because we've removed the
# libraries with text relocations (FFmpeg).
target_link_options(gstreamer-player PRIVATE
    "-Wl,-z,max-page-size=4096"
)
