/**
 * gstreamer-jni.cpp — JNI bridge between Kotlin and the native GStreamer player.
 *
 * This file exposes the following native methods to Kotlin:
 *
 *   native nativeInit(nativeLibDir: String, cacheDir: String) : Boolean
 *   native nativeSetPipeline(pipeline: String)
 *   native nativeStart(surface: Surface) : Boolean
 *   native nativeStop()
 *   native nativeDeinit()
 *   native nativeIsPlaying() : Boolean
 *   native nativeGetGStreamerVersion() : String
 *
 * The native library is loaded in Kotlin via:
 *   System.loadLibrary("gstreamer-player")
 */

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <gst/gst.h>

#include "gst-player.h"

#define LOG_TAG "RtpPlayer_JNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ── Global player instance ─────────────────────────────────────────────
// A single global instance is fine for this MVP — one app, one stream,
// one player. This avoids complexity with object lifecycle management.
static GstPlayer* gPlayer = nullptr;

// ── Native method implementations ──────────────────────────────────────

extern "C" {

/**
 * Called once during app startup to initialize GStreamer.
 *
 * @param nativeLibDir  Path to the app's native library directory
 *                      (e.g., /data/app/.../lib/arm64). GStreamer will
 *                      look for plugin .so files here.
 * @param cacheDir      Path to the app's cache directory. GStreamer
 *                      will store its plugin registry here.
 * @return true on success
 */
JNIEXPORT jboolean JNICALL
Java_com_local_rtpplayer_GStreamer_nativeInit(JNIEnv* env, jclass /*clazz*/,
                                               jstring nativeLibDirJstr,
                                               jstring cacheDirJstr) {
    LOGD("nativeInit — entering");

    if (gPlayer) {
        LOGW("nativeInit — player already exists, reusing");
        return JNI_TRUE;
    }

    // Get the C-string paths.
    const char* nativeLibDir = env->GetStringUTFChars(nativeLibDirJstr, nullptr);
    const char* cacheDir     = env->GetStringUTFChars(cacheDirJstr, nullptr);

    if (!nativeLibDir || !cacheDir) {
        LOGE("nativeInit — GetStringUTFChars failed");
        if (nativeLibDir) env->ReleaseStringUTFChars(nativeLibDirJstr, nativeLibDir);
        if (cacheDir) env->ReleaseStringUTFChars(cacheDirJstr, cacheDir);
        return JNI_FALSE;
    }

    // ── Set GStreamer environment variables for Android ───────────────

    // Tell GStreamer where to find plugin .so files.
    // All .so files (core + plugins) are in the flat native lib directory.
    g_setenv("GST_PLUGIN_PATH", nativeLibDir, TRUE);
    LOGD("nativeInit — GST_PLUGIN_PATH=%s", nativeLibDir);

    // Disable system-wide plugin scanning (speeds up init, avoids
    // scanning irrelevant directories on the device).
    g_setenv("GST_PLUGIN_SYSTEM_PATH", "", TRUE);

    // Set the plugin registry cache location.
    std::string registryPath = std::string(cacheDir) + "/gst-registry.bin";
    g_setenv("GST_REGISTRY", registryPath.c_str(), TRUE);
    LOGD("nativeInit — GST_REGISTRY=%s", registryPath.c_str());

    // Allow the registry to be rebuilt on each run (useful during
    // development; can be disabled later for faster startup).
    g_setenv("GST_REGISTRY_UPDATE", "yes", TRUE);
    g_setenv("GST_REGISTRY_FORK", "no", TRUE);

    env->ReleaseStringUTFChars(nativeLibDirJstr, nativeLibDir);
    env->ReleaseStringUTFChars(cacheDirJstr, cacheDir);

    // ── Create and initialize the player ──────────────────────────────
    gPlayer = new GstPlayer();
    if (!gPlayer) {
        LOGE("nativeInit — failed to allocate GstPlayer");
        return JNI_FALSE;
    }

    bool ok = gPlayer->init();
    if (!ok) {
        LOGE("nativeInit — GstPlayer::init() failed");
        delete gPlayer;
        gPlayer = nullptr;
        return JNI_FALSE;
    }

    LOGI("nativeInit — success");
    return JNI_TRUE;
}

/**
 * Set the pipeline description string.
 * Should be called before nativeStart().
 */
JNIEXPORT void JNICALL
Java_com_local_rtpplayer_GStreamer_nativeSetPipeline(JNIEnv* env, jclass /*clazz*/,
                                                      jstring pipelineJstr) {
    LOGD("nativeSetPipeline — entering");

    if (!gPlayer) {
        LOGE("nativeSetPipeline — player not initialized");
        return;
    }

    const char* pipelineCstr = env->GetStringUTFChars(pipelineJstr, nullptr);
    if (!pipelineCstr) {
        LOGE("nativeSetPipeline — GetStringUTFChars failed");
        return;
    }

    gPlayer->setPipeline(std::string(pipelineCstr));
    LOGI("nativeSetPipeline — pipeline string set");

    env->ReleaseStringUTFChars(pipelineJstr, pipelineCstr);
}

/**
 * Start the pipeline, rendering to the given Surface.
 * Returns true on success.
 */
JNIEXPORT jboolean JNICALL
Java_com_local_rtpplayer_GStreamer_nativeStart(JNIEnv* env, jclass /*clazz*/,
                                                jobject surface) {
    LOGD("nativeStart — entering");

    if (!gPlayer) {
        LOGE("nativeStart — player not initialized");
        return JNI_FALSE;
    }

    if (!surface) {
        LOGE("nativeStart — null Surface provided");
        return JNI_FALSE;
    }

    // Convert the Java Surface to an ANativeWindow.
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("nativeStart — ANativeWindow_fromSurface failed");
        return JNI_FALSE;
    }

    LOGI("nativeStart — ANativeWindow obtained: %dx%d",
         ANativeWindow_getWidth(window),
         ANativeWindow_getHeight(window));

    bool ok = gPlayer->start(window);

    // Release our reference to the ANativeWindow.
    // The pipeline holds its own reference internally.
    ANativeWindow_release(window);

    if (!ok) {
        LOGE("nativeStart — GstPlayer::start() failed");
        return JNI_FALSE;
    }

    LOGI("nativeStart — pipeline started successfully");
    return JNI_TRUE;
}

/**
 * Stop the pipeline.
 * Safe to call even if the pipeline is not running.
 */
JNIEXPORT void JNICALL
Java_com_local_rtpplayer_GStreamer_nativeStop(JNIEnv* env, jclass /*clazz*/) {
    LOGD("nativeStop — entering");

    if (!gPlayer) {
        LOGW("nativeStop — player not initialized, nothing to stop");
        return;
    }

    gPlayer->stop();
    LOGI("nativeStop — pipeline stopped");
}

/**
 * Full reset — stop pipeline, deinit, and destroy the player instance.
 * After this, nativeInit() must be called before nativeStart().
 */
JNIEXPORT void JNICALL
Java_com_local_rtpplayer_GStreamer_nativeReset(JNIEnv* env, jclass /*clazz*/) {
    LOGD("nativeReset — entering");

    if (!gPlayer) {
        LOGW("nativeReset — player not initialized, nothing to reset");
        return;
    }

    gPlayer->reset();
    delete gPlayer;
    gPlayer = nullptr;

    LOGI("nativeReset — player fully reset");
}

/**
 * Release all GStreamer resources.
 * After calling this, nativeInit() must be called again.
 */
JNIEXPORT void JNICALL
Java_com_local_rtpplayer_GStreamer_nativeDeinit(JNIEnv* env, jclass /*clazz*/) {
    LOGD("nativeDeinit — entering");

    if (!gPlayer) {
        LOGW("nativeDeinit — player not initialized, nothing to deinit");
        return;
    }

    gPlayer->deinit();
    delete gPlayer;
    gPlayer = nullptr;

    LOGI("nativeDeinit — done");
}

/**
 * Return true if the pipeline is currently in the PLAYING state.
 */
JNIEXPORT jboolean JNICALL
Java_com_local_rtpplayer_GStreamer_nativeIsPlaying(JNIEnv* env, jclass /*clazz*/) {
    if (!gPlayer) {
        return JNI_FALSE;
    }
    return gPlayer->isPlaying() ? JNI_TRUE : JNI_FALSE;
}

/**
 * Return true if a stream error has been detected.
 */
JNIEXPORT jboolean JNICALL
Java_com_local_rtpplayer_GStreamer_nativeHasError(JNIEnv* env, jclass /*clazz*/) {
    if (!gPlayer) {
        return JNI_TRUE;
    }
    return gPlayer->hasError() ? JNI_TRUE : JNI_FALSE;
}

/**
 * Return a human-readable description of the last error.
 */
JNIEXPORT jstring JNICALL
Java_com_local_rtpplayer_GStreamer_nativeGetLastError(JNIEnv* env, jclass /*clazz*/) {
    if (!gPlayer) {
        return env->NewStringUTF("Player not initialized");
    }
    std::string err = gPlayer->getLastError();
    if (err.empty()) {
        err = "No error";
    }
    return env->NewStringUTF(err.c_str());
}

/**
 * Return the GStreamer version string (for diagnostics).
 */
JNIEXPORT jstring JNICALL
Java_com_local_rtpplayer_GStreamer_nativeGetGStreamerVersion(JNIEnv* env, jclass /*clazz*/) {
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);

    char versionStr[64];
    snprintf(versionStr, sizeof(versionStr), "%u.%u.%u", major, minor, micro);

    LOGD("nativeGetGStreamerVersion — %s", versionStr);
    return env->NewStringUTF(versionStr);
}

} // extern "C"
