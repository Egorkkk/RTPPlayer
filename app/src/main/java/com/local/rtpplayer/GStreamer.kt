package com.local.rtpplayer

import android.content.Context
import android.util.Log
import android.view.Surface

/**
 * Kotlin-side GStreamer facade.
 *
 * This class handles:
 *  1. Loading all required GStreamer .so files in dependency order
 *  2. Setting up the plugin path so GStreamer can find its plugins
 *  3. Wrapping JNI calls for pipeline lifecycle
 *
 * Usage:
 *   1. Call GStreamer.init(context) once during app startup.
 *   2. Call GStreamer.setPipeline("...") to configure the pipeline.
 *   3. Call GStreamer.start(surface) to begin playback.
 *   4. Call GStreamer.stop() to pause/stop.
 *   5. Call GStreamer.deinit() during app teardown.
 */
object GStreamer {

    private const val TAG = "RtpPlayer_GStreamer"

    // Track whether libraries have been loaded.
    @Volatile
    private var libsLoaded = false

    // ── Native method declarations ────────────────────────────────────

    @JvmStatic
    external fun nativeInit(nativeLibDir: String, cacheDir: String): Boolean
    @JvmStatic
    external fun nativeSetPipeline(pipeline: String)
    @JvmStatic
    external fun nativeStart(surface: Surface): Boolean
    @JvmStatic
    external fun nativeStop()
    @JvmStatic
    external fun nativeReset()
    @JvmStatic
    external fun nativeDeinit()
    @JvmStatic
    external fun nativeIsPlaying(): Boolean
    @JvmStatic
    external fun nativeHasError(): Boolean
    @JvmStatic
    external fun nativeGetLastError(): String
    @JvmStatic
    external fun nativeGetGStreamerVersion(): String

    // ── Library Loading ───────────────────────────────────────────────

    /**
     * Load all required GStreamer .so files in dependency order.
     * This must be called before any other GStreamer method.
     *
     * The dependency order matters: core libraries must be loaded before
     * plugins, and plugins before our bridge library.
     *
     * @param context Application context (for native library directory path)
     * @return true if all libraries loaded successfully
     */
    fun loadLibraries(context: Context): Boolean {
        if (libsLoaded) {
            return true
        }

        // On Android, native libraries from the APK are extracted to:
        //   /data/app/<package>/lib/<abi>/
        // We can get this path from Context.getApplicationInfo().nativeLibraryDir.
        val nativeLibDir = context.applicationInfo.nativeLibraryDir
        Log.d(TAG, "Native library directory: $nativeLibDir")

        // Our bridge library contains everything:
        //   - The JNI implementation (gstreamer-jni.cpp)
        //   - The C++ player class (gst-player.cpp)
        //   - Static GStreamer core (libgstreamer-1.0.a)
        //   - Static GLib (libglib-2.0.a, libgobject-2.0.a)
        //   - Static dependencies (libintl, libffi, libpcre2, libiconv)
        val mainLib = "gstreamer-player"

        if (!loadLibSafe(mainLib)) {
            Log.e(TAG, "Failed to load main library: $mainLib")
            return false
        }

        Log.d(TAG, "GStreamer player library loaded successfully")

        // Mark as loaded. Plugin .so files will be found by GStreamer
        // via the GST_PLUGIN_PATH set in nativeInit().
        libsLoaded = true
        return true
    }

    /**
     * Load a single .so library by name using System.loadLibrary.
     * Returns true on success, false on failure.
     */
    private fun loadLibSafe(name: String): Boolean {
        return try {
            System.loadLibrary(name)
            Log.d(TAG, "  Loaded: lib$name.so")
            true
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "  Skipping (not found): lib$name.so — ${e.message}")
            false
        }
    }

    // ── Public API ────────────────────────────────────────────────────

    /**
     * One-time GStreamer initialization.
     * Must be called before any other method.
     *
     * @param context Application context
     * @return true on success
     */
    fun init(context: Context): Boolean {
        // Ensure libraries are loaded.
        if (!libsLoaded) {
            if (!loadLibraries(context)) {
                Log.e(TAG, "init — failed to load GStreamer libraries")
                return false
            }
        }

        Log.d(TAG, "init — calling native init")

        val nativeLibDir = context.applicationInfo.nativeLibraryDir
        val cacheDir = context.cacheDir.absolutePath

        val ok = nativeInit(nativeLibDir, cacheDir)
        if (ok) {
            val version = nativeGetGStreamerVersion()
            Log.i(TAG, "init — GStreamer version: $version")
        } else {
            Log.e(TAG, "init — native init failed")
        }
        return ok
    }

    /**
     * Configure the pipeline description string.
     * Call before start(), or between stop() and start().
     */
    fun setPipeline(pipeline: String) {
        if (!libsLoaded) {
            Log.e(TAG, "setPipeline — libraries not loaded, ignoring")
            return
        }
        Log.d(TAG, "setPipeline — configuring pipeline")
        nativeSetPipeline(pipeline)
    }

    /**
     * Start the pipeline, rendering to the given Surface.
     */
    fun start(surface: Surface): Boolean {
        if (!libsLoaded) {
            Log.e(TAG, "start — libraries not loaded")
            return false
        }
        Log.d(TAG, "start — calling native start")
        val ok = nativeStart(surface)
        if (ok) {
            Log.i(TAG, "start — pipeline running")
        } else {
            Log.e(TAG, "start — native start failed")
        }
        return ok
    }

    /**
     * Stop the pipeline.
     * Safe to call even if not running.
     */
    fun stop() {
        if (!libsLoaded) return
        Log.d(TAG, "stop — calling native stop")
        nativeStop()
    }

    /**
     * Full reset — stop pipeline and deinitialize GStreamer.
     * After calling this, init() must be called again before start().
     * Use this if the player enters an unknown/broken state.
     */
    fun reset() {
        if (libsLoaded) {
            Log.d(TAG, "reset — calling native reset (full reset)")
            nativeReset()
            libsLoaded = false // force library reload on next init()
        }
    }

    /**
     * Release all GStreamer resources.
     */
    fun deinit() {
        if (libsLoaded) {
            Log.d(TAG, "deinit — calling native deinit")
            nativeDeinit()
        }
    }

    /**
     * Check if pipeline is currently playing.
     */
    fun isPlaying(): Boolean {
        if (!libsLoaded) return false
        return nativeIsPlaying()
    }

    /**
     * Check if a stream error has been detected (bus ERROR or no-data timeout).
     * The flag is cleared when stop() is called.
     */
    fun hasError(): Boolean {
        if (!libsLoaded) return true // Treat as error if not loaded
        return nativeHasError()
    }

    /**
     * Get the last error description.
     * Returns "No error" if no error has occurred.
     */
    fun getLastError(): String {
        if (!libsLoaded) return "Libraries not loaded"
        return nativeGetLastError()
    }

    /**
     * Get GStreamer version string for diagnostics.
     */
    fun getVersion(): String {
        if (!libsLoaded) return "Unknown"
        return nativeGetGStreamerVersion()
    }
}
