package com.local.rtpplayer

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.SurfaceHolder
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * Main and only activity.
 *
 * Lifecycle state machine:
 *
 *   onResume() ──────────────▶ wantsRunning = true
 *   onPause()  ──────────────▶ wantsRunning = false
 *   surfaceCreated() ────────▶ surfaceReady = true
 *   surfaceDestroyed() ──────▶ surfaceReady = false
 *
 * The pipeline is started only when BOTH conditions are true:
 *   wantsRunning && surfaceReady
 *
 * The pipeline is stopped when EITHER condition becomes false.
 *
 * A delayed retry mechanism handles surface recreate races and
 * transient failures.
 *
 * A periodic status checker polls the native layer for stream
 * errors (bus ERROR or no-data timeout) and triggers a reconnect
 * if one is detected.
 *
 * Stream is hardcoded to UDP port 5600, RTP/H.265.
 */
class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "RtpPlayer"

        // Delay in ms before retrying a failed pipeline start.
        private const val RETRY_DELAY_MS = 500L

        // Interval in ms for polling the native layer for stream errors.
        // This is how often we check for "no signal" conditions.
        private const val STATUS_CHECK_INTERVAL_MS = 2000L

        // Maximum reconnect attempts before showing "No Signal" permanently.
        // After this many consecutive failures, we stop auto-reconnecting
        // and just show the overlay. Reconnect resumes on resume/surface change.
        private const val MAX_RECONNECT_ATTEMPTS = 10
    }

    // Fullscreen surface reserved for the Android MediaCodec stage.
    private lateinit var surfaceView: android.view.SurfaceView

    // "No Signal" overlay TextView.
    private lateinit var noSignalText: TextView

    // ── Lifecycle state ───────────────────────────────────────────────

    // Whether the activity is in the foreground (onResume → onPause).
    private var wantsRunning = false

    // Whether the surface exists and is valid for rendering.
    private var surfaceReady = false

    // Whether GStreamer was initialized successfully.
    private var gstreamerInitDone = false

    // Whether the pipeline is currently running (for logging clarity).
    private var pipelineRunning = false

    // Consecutive reconnect failure count.
    // Reset on successful start or when going to background.
    private var reconnectAttempts = 0

    // ── Handlers and runnables ────────────────────────────────────────

    // Retry handler — for retrying failed pipeline starts.
    private val retryHandler = Handler(Looper.getMainLooper())
    private val retryRunnable = Runnable {
        Log.d(TAG, "retryRunnable — retrying pipeline start")
        tryStartPipeline()
    }

    // Status checker — polls native layer for stream errors.
    private val statusHandler = Handler(Looper.getMainLooper())
    private val statusRunnable = object : Runnable {
        override fun run() {
            checkStreamStatus()
            if (wantsRunning) {
                statusHandler.postDelayed(this, STATUS_CHECK_INTERVAL_MS)
            }
        }
    }

    // ── Pipeline configuration ────────────────────────────────────────
    //
    // Edit this string to change UDP port, latency, decoder, etc.
    //
    // Active branch path: ingest/jitter/depay/parse only.
    // Decode/render moves to Android MediaCodec and will be wired later.
    //
    private val defaultPipeline = buildString {
        append("udpsrc name=udpsrc0 port=5600 buffer-size=7000 ! ")
        append("application/x-rtp,media=video,encoding-name=H265,payload=96 ! ")
        append("rtpjitterbuffer latency=0 drop-on-latency=true ! ")
        append("rtph265depay ! ")
        append("h265parse config-interval=-1 ! ")
        append("appsink name=hevcappsink emit-signals=false sync=false max-buffers=8 drop=true")
    }

    // ── Activity Lifecycle ──────────────────────────────────────────

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        logLifecycle("onCreate")

        // Hide system bars for immersive fullscreen
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            )

        surfaceView = findViewById(R.id.surface_view)
        surfaceView.holder.addCallback(this)

        noSignalText = findViewById(R.id.no_signal_text)

        // Keep screen on during playback
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Initialize GStreamer once.
        gstreamerInitDone = GStreamer.init(applicationContext)
        if (gstreamerInitDone) {
            Log.i(TAG, "GStreamer initialized, version: ${GStreamer.getVersion()}")
        } else {
            Log.e(TAG, "GStreamer init FAILED — playback will not work")
            showNoSignal("GStreamer failed to initialize")
        }
    }

    override fun onResume() {
        super.onResume()
        logLifecycle("onResume — entering foreground")
        wantsRunning = true

        // Reset reconnect counter when coming to foreground.
        reconnectAttempts = 0

        // Configure the pipeline string (idempotent).
        GStreamer.setPipeline(defaultPipeline)

        // Start the periodic status checker.
        statusHandler.postDelayed(statusRunnable, STATUS_CHECK_INTERVAL_MS)

        // Attempt to start — will only succeed if surface is also ready.
        tryStartPipeline()
    }

    override fun onPause() {
        logLifecycle("onPause — leaving foreground")
        wantsRunning = false

        // Cancel all pending handlers.
        retryHandler.removeCallbacks(retryRunnable)
        statusHandler.removeCallbacks(statusRunnable)

        // Reset reconnect counter.
        reconnectAttempts = 0

        // Stop the pipeline.
        stopPipeline()

        // Hide the "No Signal" overlay when going to background.
        hideNoSignal()

        super.onPause()
    }

    override fun onDestroy() {
        logLifecycle("onDestroy")
        wantsRunning = false
        surfaceReady = false

        // Cancel all handlers.
        retryHandler.removeCallbacks(retryRunnable)
        statusHandler.removeCallbacks(statusRunnable)

        // Ensure pipeline is stopped.
        stopPipeline()

        // Deinitialize GStreamer.
        if (gstreamerInitDone) {
            GStreamer.deinit()
            gstreamerInitDone = false
        }

        super.onDestroy()
    }

    // ── SurfaceHolder.Callback ──────────────────────────────────────

    override fun surfaceCreated(holder: SurfaceHolder) {
        logLifecycle("surfaceCreated")
        surfaceReady = true
        tryStartPipeline()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        logLifecycle("surfaceChanged — ${width}x${height}, format=$format")

        // If the surface changes dimensions while the pipeline is running,
        // we need to restart the pipeline so it picks up the new surface.
        if (pipelineRunning) {
            Log.d(TAG, "surfaceChanged — surface changed while running, restarting pipeline")
            restartPipeline()
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        logLifecycle("surfaceDestroyed")
        surfaceReady = false

        // Cancel retry — the surface is gone.
        retryHandler.removeCallbacks(retryRunnable)

        stopPipeline()
    }

    // ── Internal helpers ────────────────────────────────────────────

    /**
     * Attempt to start the GStreamer pipeline.
     * Only succeeds if all conditions are met:
     *   - GStreamer was initialized
     *   - Activity is in foreground (wantsRunning)
     *   - Surface is ready
     *
     * If start() fails but conditions still look valid, schedules
     * one retry after a short delay.
     */
    private fun tryStartPipeline() {
        // Check all conditions.
        if (!gstreamerInitDone) {
            Log.w(TAG, "tryStartPipeline — GStreamer not initialized")
            return
        }
        if (!wantsRunning) {
            Log.d(TAG, "tryStartPipeline — not in foreground, skipping")
            return
        }
        if (!surfaceReady) {
            Log.d(TAG, "tryStartPipeline — surface not ready yet, waiting")
            return
        }
        if (pipelineRunning) {
            Log.d(TAG, "tryStartPipeline — pipeline already running")
            return
        }

        // Hide "No Signal" while attempting to start.
        hideNoSignal()

        // Ensure the pipeline is fully stopped before starting.
        GStreamer.stop()

        val holder = surfaceView.holder
        val ok = GStreamer.start(holder.surface)

        if (ok) {
            pipelineRunning = true
            reconnectAttempts = 0
            Log.i(TAG, "tryStartPipeline — parse-only ingest pipeline started successfully")
        } else {
            reconnectAttempts++
            Log.e(TAG, "tryStartPipeline — pipeline start FAILED (attempt $reconnectAttempts/$MAX_RECONNECT_ATTEMPTS)")

            if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
                showNoSignal("No Signal — reconnect failed\ncheck network connection")
                Log.w(TAG, "tryStartPipeline — max reconnect attempts reached, stopping retries")
                return
            }

            // If conditions still look valid, schedule one retry.
            if (wantsRunning && surfaceReady && gstreamerInitDone) {
                Log.d(TAG, "tryStartPipeline — conditions still valid, scheduling retry in ${RETRY_DELAY_MS}ms")
                retryHandler.postDelayed(retryRunnable, RETRY_DELAY_MS)
            }
        }
    }

    /**
     * Stop the pipeline and update internal state.
     * Safe to call even if the pipeline is not running.
     */
    private fun stopPipeline() {
        if (!pipelineRunning) {
            Log.d(TAG, "stopPipeline — pipeline not running, nothing to stop")
            return
        }

        GStreamer.stop()
        pipelineRunning = false
        Log.i(TAG, "stopPipeline — pipeline stopped")
    }

    /**
     * Restart the pipeline (stop + start).
     * Used when the surface changes dimensions.
     */
    private fun restartPipeline() {
        Log.d(TAG, "restartPipeline — stopping and restarting")
        pipelineRunning = false
        GStreamer.stop()
        GStreamer.setPipeline(defaultPipeline)
        tryStartPipeline()
    }

    /**
     * Check the native layer for stream errors or no-data conditions.
     * Called periodically by the status checker handler.
     */
    private fun checkStreamStatus() {
        if (!pipelineRunning) {
            return
        }

        if (GStreamer.hasError()) {
            val error = GStreamer.getLastError()
            Log.w(TAG, "checkStreamStatus — error detected: $error")

            // Stop the current pipeline.
            pipelineRunning = false
            GStreamer.stop()

            // Show "No Signal" overlay.
            showNoSignal("No Signal")

            // Attempt to reconnect (one retry with delay).
            reconnectAttempts++
            if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
                Log.d(TAG, "checkStreamStatus — scheduling reconnect retry in ${RETRY_DELAY_MS}ms")
                retryHandler.postDelayed(retryRunnable, RETRY_DELAY_MS)
            } else {
                Log.w(TAG, "checkStreamStatus — max reconnect attempts reached")
            }
        }
    }

    /**
     * Show the "No Signal" overlay with an optional message.
     */
    private fun showNoSignal(message: String? = null) {
        if (message != null) {
            noSignalText.text = message
        }
        noSignalText.visibility = View.VISIBLE
        Log.d(TAG, "showNoSignal — visible (message: ${message ?: noSignalText.text})")
    }

    /**
     * Hide the "No Signal" overlay.
     */
    private fun hideNoSignal() {
        noSignalText.visibility = View.GONE
    }

    /**
     * Log a lifecycle event with a consistent tag and timestamp-friendly format.
     */
    private fun logLifecycle(event: String) {
        Log.d(
            TAG,
            "[$event] wantsRunning=$wantsRunning surfaceReady=$surfaceReady " +
                "pipelineRunning=$pipelineRunning reconnectAttempts=$reconnectAttempts"
        )
    }
}
