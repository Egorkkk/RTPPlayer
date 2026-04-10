#ifndef GST_PLAYER_H
#define GST_PLAYER_H

/**
 * gst-player.h — Native GStreamer player wrapper.
 *
 * Provides a simple C++ class that owns the GStreamer pipeline lifecycle:
 *   - init()     : one-time GStreamer initialization
 *   - setPipeline() : configure the pipeline string
 *   - start(surface) : build pipeline and start rendering to a Surface
 *   - stop()     : tear down the pipeline
 *   - deinit()   : free GStreamer resources
 *
 * Thread safety:
 *   - All public methods must be called from a single thread (the main/UI
 *     thread via JNI). GStreamer runs its own internal threads for data flow.
 *   - The GLib main loop runs in a dedicated background thread for bus
 *     message processing.
 *   - Error state and timeout tracking are updated on the GLib main loop
 *     thread and read from the JNI thread via atomic flags.
 */

#include <android/native_window.h>
#include <gst/gst.h>
#include <atomic>
#include <pthread.h>
#include <string>

typedef struct _GstAppSink GstAppSink;

// Forward declarations for GLib/GStreamer types if we didn't include gst.h
// But since we included gst.h, we can just use them.

// How long (seconds) without receiving any data before we consider the
// stream "missing". This is checked via a periodic timer on the bus.
static const int STREAM_TIMEOUT_SECONDS = 5;

class GstPlayer {
public:
    GstPlayer();
    ~GstPlayer();

    // One-time GStreamer library initialization.
    // Must be called before any other method.
    bool init();

    // Set the pipeline description string.
    // Call before start(), or between stop() and start().
    void setPipeline(const std::string& pipelineDesc);

    // Build and start the pipeline, rendering to the given ANativeWindow.
    // Returns true on success.
    bool start(ANativeWindow* window);

    // Stop and tear down the pipeline.
    // Safe to call even if the pipeline is not running.
    // After calling stop(), start() can be called again to restart.
    void stop();

    // Fully reset the player to its initial constructed state.
    // Call this if the player is in an unknown/broken state.
    // After reset(), init() must be called again before start().
    void reset();

    // Release all GStreamer resources.
    // After calling this, init() must be called again before start().
    void deinit();

    // Check if the pipeline is currently in PLAYING state.
    bool isPlaying() const;

    // ── Stream status (thread-safe) ─────────────────────────────────

    // Friend declarations for GLib callbacks to access private members
    friend gboolean timeout_check_callback(gpointer userData);
    friend gboolean bus_message_callback(GstBus* bus, GstMessage* msg, gpointer userData);

    // Returns true if a stream error has been detected (bus ERROR or
    // timeout with no data received). The flag is cleared by stop().
    bool hasError() const;

    // Returns a human-readable description of the last error.
    std::string getLastError() const;

private:
    // Internal helpers
    bool buildPipeline();
    void resetSampleStats();
    static GstFlowReturn onNewSampleThunk(GstAppSink* sink, gpointer userData);
    GstFlowReturn onNewSample(GstAppSink* sink);
    bool configureUdpsrcSocket(GstElement* udpsrc);

    // GLib main loop thread
    void startMainLoop();
    void stopMainLoop();
    static void* mainLoopThreadFunc(void* userData);

    // Pipeline handle (opaque GstElement pointer)
    void* pipeline_ = nullptr;

    bool initialized_ = false;
    bool running_ = false;

    // GLib main loop infrastructure
    void* mainLoop_ = nullptr;       // GMainLoop*
    void* mainContext_ = nullptr;    // GMainContext*
    pthread_t mainLoopThread_ = {};
    bool mainLoopRunning_ = false;

    // Bus message callback ID (source ID from g_source_attach)
    unsigned int busWatchId_ = 0;

    // Timeout timer source ID (for periodic no-data checks)
    unsigned int timeoutSourceId_ = 0;

    // Current pipeline description
    std::string pipelineDesc_;

    // ── Error / timeout tracking ─────────────────────────────────────
    // These are updated on the GLib main loop thread and read from the
    // JNI thread. We use atomic operations for thread safety.

    // Timestamp (monotonic seconds) of the last time we saw meaningful
    // data flow (a STREAM_STATUS, ASYNC_DONE, or non-state-change message
    // from a non-udpsrc element). Set to 0 on stop().
    int64_t lastDataTime_ = 0;

    // Error flag — set to true when a bus ERROR is received or when
    // the no-data timeout fires. Cleared by stop().
    std::atomic<bool> hasError_{false};

    // Last error message (protected by the GLib main loop thread —
    // only written on that thread, read after stop() clears it).
    std::string lastError_;

    // The udpsrc element — we use it to check byte counters for data flow.
    void* udpsrcElement_ = nullptr;

    // The appsink element that receives parsed HEVC output after h265parse.
    void* appSinkElement_ = nullptr;

    // The externally created UDP socket passed into udpsrc for diagnostics.
    void* externalUdpSocket_ = nullptr;

    // Parser/appsink diagnostics.
    std::atomic<uint64_t> sampleCount_{0};
    std::atomic<uint64_t> sampleBytes_{0};
    bool sawVps_ = false;
    bool sawSps_ = false;
    bool sawPps_ = false;
};

#endif // GST_PLAYER_H
