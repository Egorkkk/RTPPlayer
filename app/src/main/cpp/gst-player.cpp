#include "gst-player.h"

#include <android/log.h>
#include <gst/gst.h>

#include <chrono>

// ── Static Plugin Declarations ────────────────────────────────────────
// Since we are linking plugins statically, we must declare and register them.
#include <gst/gst.h>

extern "C" {
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(udp);
GST_PLUGIN_STATIC_DECLARE(rtp);
GST_PLUGIN_STATIC_DECLARE(rtpmanager);
GST_PLUGIN_STATIC_DECLARE(videoparsersbad);
GST_PLUGIN_STATIC_DECLARE(isomp4);
GST_PLUGIN_STATIC_DECLARE(androidmedia);
GST_PLUGIN_STATIC_DECLARE(videoconvertscale);
GST_PLUGIN_STATIC_DECLARE(playback);
}

#define LOG_TAG "GstPlayerNative"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ── Helper macros ─────────────────────────────────────────────────────
#define GST_MSG_CAST(ptr)       static_cast<GstMessage*>(ptr)
#define GST_MAIN_LOOP_CAST(ptr) static_cast<GMainLoop*>(ptr)
#define GST_MAIN_CTX_CAST(ptr)  static_cast<GMainContext*>(ptr)

// ── Diagnostics ───────────────────────────────────────────────────────
static void logFactoryDiagnostic(const char* factoryName) {
    GstElementFactory* factory = gst_element_factory_find(factoryName);
    if (!factory) {
        LOGW("GST_DIAG factory=%s status=MISSING", factoryName);
        return;
    }

    const gchar* pluginName =
        gst_plugin_feature_get_plugin_name(GST_PLUGIN_FEATURE(factory));
    const gchar* longName =
        gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_LONGNAME);

    LOGI("GST_DIAG factory=%s status=FOUND plugin=%s long-name=%s",
         factoryName,
         pluginName ? pluginName : "(unknown)",
         longName ? longName : "(unknown)");

    gst_object_unref(factory);
}

static void logRequiredFactoryDiagnostics() {
    LOGI("GST_DIAG checking required element factories");
    logFactoryDiagnostic("udpsrc");
    logFactoryDiagnostic("rtpjitterbuffer");
    logFactoryDiagnostic("rtph265depay");
    logFactoryDiagnostic("h265parse");
    logFactoryDiagnostic("androidvideosink");
    logFactoryDiagnostic("amcviddec-omxgoogleh265decoder");
}

// ── Time helper ───────────────────────────────────────────────────────
static int64_t nowSeconds() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

// ── Constructor / Destructor ──────────────────────────────────────────

GstPlayer::GstPlayer() {
    LOGD("GstPlayer constructed");
}

GstPlayer::~GstPlayer() {
    stop();
    deinit();
    LOGD("GstPlayer destroyed");
}

// ── Init / Deinit ─────────────────────────────────────────────────────

bool GstPlayer::init() {
    if (initialized_) {
        LOGW("GstPlayer::init — already initialized, skipping");
        return true;
    }

    // Register static plugins before gst_init
    LOGI("GST_DIAG static plugin registration starting");
    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(udp);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
    GST_PLUGIN_STATIC_REGISTER(videoparsersbad);
    GST_PLUGIN_STATIC_REGISTER(isomp4);
    GST_PLUGIN_STATIC_REGISTER(androidmedia);
    GST_PLUGIN_STATIC_REGISTER(videoconvertscale);
    GST_PLUGIN_STATIC_REGISTER(playback);
    LOGI("GST_DIAG static plugin registration finished");

    GError* error = nullptr;
    gboolean result = gst_init_check(nullptr, nullptr, &error);

    if (!result) {
        LOGE("GstPlayer::init — gst_init_check failed: %s",
             error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    initialized_ = true;
    logRequiredFactoryDiagnostics();
    LOGI("GstPlayer::init — GStreamer initialized successfully");
    return true;
}

void GstPlayer::deinit() {
    if (!initialized_) {
        return;
    }

    stop();

    // We do NOT call gst_deinit() on Android.
    initialized_ = false;
    LOGI("GstPlayer::deinit — resources released");
}

// ── Pipeline Configuration ────────────────────────────────────────────

void GstPlayer::setPipeline(const std::string& pipelineDesc) {
    if (running_) {
        LOGW("GstPlayer::setPipeline — pipeline is running, call stop() first");
    }
    pipelineDesc_ = pipelineDesc;
    LOGD("GstPlayer::setPipeline — \"%s\"", pipelineDesc.c_str());
}

// ── GLib Main Loop Thread ─────────────────────────────────────────────

void* GstPlayer::mainLoopThreadFunc(void* userData) {
    auto* player = static_cast<GstPlayer*>(userData);
    LOGD("GLib main loop thread started");
    g_main_loop_run(GST_MAIN_LOOP_CAST(player->mainLoop_));
    LOGD("GLib main loop thread exited");
    return nullptr;
}

void GstPlayer::startMainLoop() {
    if (mainLoopRunning_) {
        LOGW("startMainLoop — already running");
        return;
    }

    mainContext_ = g_main_context_new();
    mainLoop_ = g_main_loop_new(GST_MAIN_CTX_CAST(mainContext_), FALSE);
    mainLoopRunning_ = true;

    int ret = pthread_create(&mainLoopThread_, nullptr, mainLoopThreadFunc, this);
    if (ret != 0) {
        LOGE("startMainLoop — pthread_create failed: %d", ret);
        mainLoopRunning_ = false;
        g_main_loop_unref(GST_MAIN_LOOP_CAST(mainLoop_));
        mainLoop_ = nullptr;
        g_main_context_unref(GST_MAIN_CTX_CAST(mainContext_));
        mainContext_ = nullptr;
        return;
    }

    LOGI("startMainLoop — GLib main loop thread started");
}

void GstPlayer::stopMainLoop() {
    if (!mainLoopRunning_) {
        return;
    }

    if (mainLoop_) {
        g_main_loop_quit(GST_MAIN_LOOP_CAST(mainLoop_));
    }

    pthread_join(mainLoopThread_, nullptr);

    if (mainLoop_) {
        g_main_loop_unref(GST_MAIN_LOOP_CAST(mainLoop_));
        mainLoop_ = nullptr;
    }
    if (mainContext_) {
        g_main_context_unref(GST_MAIN_CTX_CAST(mainContext_));
        mainContext_ = nullptr;
    }

    mainLoopRunning_ = false;
    LOGI("stopMainLoop — GLib main loop thread stopped");
}

// ── Build Pipeline ────────────────────────────────────────────────────

bool GstPlayer::buildPipeline() {
    if (pipelineDesc_.empty()) {
        LOGE("GstPlayer::buildPipeline — no pipeline description set");
        return false;
    }

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(pipelineDesc_.c_str(), &error);

    if (!pipeline) {
        LOGE("GstPlayer::buildPipeline — gst_parse_launch failed: %s",
             error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    pipeline_ = pipeline;
    LOGI("GstPlayer::buildPipeline — pipeline built successfully");
    return true;
}

// ── Timeout Check Callback ────────────────────────────────────────────
// Fires every 2 seconds on the GLib main loop. Checks whether data has
// been received recently by looking at the udpsrc element's byte counter.

gboolean timeout_check_callback(gpointer userData) {
    auto* player = static_cast<GstPlayer*>(userData);

    if (!player->udpsrcElement_) {
        return TRUE; // keep timer running
    }

    GstElement* udpsrc = GST_ELEMENT(player->udpsrcElement_);

    // Read the "bytes-served" property from udpsrc.
    guint64 bytesServed = 0;
    g_object_get(G_OBJECT(udpsrc), "bytes-served", &bytesServed, nullptr);

    LOGD("GstPlayer timeout check — udpsrc bytes-served: %llu", (unsigned long long)bytesServed);

    if (bytesServed == 0) {
        int64_t now = nowSeconds();
        int64_t elapsed = now - player->lastDataTime_;

        if (elapsed >= STREAM_TIMEOUT_SECONDS) {
            LOGW("GstPlayer timeout — no UDP data received for %lld seconds "
                 "(bytes-served=0). Stream may be absent.",
                 (long long)elapsed);

            if (!player->hasError_.load()) {
                player->hasError_.store(true);
                player->lastError_ = "No data received from UDP source. "
                                     "Check that the stream is being sent to this device.";
            }
        }
    } else {
        // Data is arriving — reset the timer and clear any previous timeout error.
        player->lastDataTime_ = nowSeconds();
        if (player->hasError_.load() &&
            player->lastError_.find("No data received") != std::string::npos) {
            player->hasError_.store(false);
            player->lastError_.clear();
            LOGI("GstPlayer timeout — data flow resumed, error cleared");
        }
    }

    return TRUE; // keep timer running
}

// ── Bus Message Callback ──────────────────────────────────────────────

gboolean bus_message_callback(GstBus* bus, GstMessage* msg, gpointer userData) {
    auto* player = static_cast<GstPlayer*>(userData);

    GstMessageType type = GST_MESSAGE_TYPE(msg);

    switch (type) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            LOGE("GstPlayer bus — ERROR: %s", err ? err->message : "unknown");
            LOGE("GstPlayer bus — debug: %s", debug ? debug : "none");

            GstObject* src = GST_MESSAGE_SRC(msg);
            if (src) {
                const gchar* elemName = gst_object_get_name(src);
                LOGE("GstPlayer bus — error source element: %s",
                     elemName ? elemName : "(unknown)");
            }

            // Record the error for the Kotlin layer to detect.
            if (err && err->message) {
                player->hasError_.store(true);
                player->lastError_ = std::string("GStreamer ERROR: ") + err->message;
            }

            if (err) g_error_free(err);
            if (debug) g_free(debug);
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(msg, &err, &debug);
            LOGW("GstPlayer bus — WARNING: %s", err ? err->message : "unknown");
            if (err) g_error_free(err);
            if (debug) g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS:
            LOGI("GstPlayer bus — End of Stream");
            break;
        case GST_MESSAGE_STATE_CHANGED:
            // Only log top-level pipeline state changes.
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(player->pipeline_)) {
                GstState oldState, newState, pending;
                gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
                LOGD("GstPlayer bus — Pipeline state: %s -> %s (pending: %s)",
                     gst_element_state_get_name(oldState),
                     gst_element_state_get_name(newState),
                     gst_element_state_get_name(pending));
            }
            break;
        case GST_MESSAGE_ASYNC_DONE:
            LOGD("GstPlayer bus — Async done (state change committed)");
            break;
        case GST_MESSAGE_REQUEST_STATE: {
            GstState requestedState;
            gst_message_parse_request_state(msg, &requestedState);
            LOGD("GstPlayer bus — Request state: %s",
                 gst_element_state_get_name(requestedState));
            break;
        }
        case GST_MESSAGE_STREAM_STATUS:
        case GST_MESSAGE_LATENCY:
        case GST_MESSAGE_CLOCK_PROVIDE:
        case GST_MESSAGE_CLOCK_LOST:
            break;
        default:
            break;
    }

    return TRUE; // keep watching
}

// ── Start / Stop ──────────────────────────────────────────────────────

bool GstPlayer::start(ANativeWindow* window) {
    if (!initialized_) {
        LOGE("GstPlayer::start — not initialized");
        return false;
    }

    if (!window) {
        LOGE("GstPlayer::start — null ANativeWindow");
        return false;
    }

    if (running_) {
        LOGW("GstPlayer::start — already running, calling stop() first");
        stop();
    }

    // Clear any previous error state.
    hasError_.store(false);
    lastError_.clear();

    int winWidth = ANativeWindow_getWidth(window);
    int winHeight = ANativeWindow_getHeight(window);
    LOGI("GstPlayer::start — ANativeWindow %p (%dx%d)", window, winWidth, winHeight);

    // Build the pipeline from the description string.
    if (!buildPipeline()) {
        LOGE("GstPlayer::start — failed to build pipeline");
        return false;
    }

    // ── Set the Android surface as the video sink target ──────────────
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "androidvideosink");

    if (!sink) {
        LOGE("GstPlayer::start — could not find element 'androidvideosink' in pipeline. "
             "Make sure the pipeline string includes: androidvideosink name=androidvideosink");
        gst_object_unref(GST_OBJECT(pipeline_));
        pipeline_ = nullptr;
        return false;
    }

    g_object_set(G_OBJECT(sink), "widget", window, nullptr);
    gst_object_unref(GST_OBJECT(sink));

    LOGI("GstPlayer::start — androidvideosink configured with ANativeWindow %p", window);

    // ── Find the udpsrc element for data flow monitoring ──────────────
    udpsrcElement_ = gst_bin_get_by_name(GST_BIN(pipeline_), "udpsrc0");
    if (udpsrcElement_) {
        // gst_bin_get_by_name may use auto-generated name.
        // Try to find any udpsrc element by iterating.
        GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline_));
        if (it) {
            gboolean done = FALSE;
            while (!done) {
                GValue item = G_VALUE_INIT;
                switch (gst_iterator_next(it, &item)) {
                    case GST_ITERATOR_OK: {
                        GstElement* elem = GST_ELEMENT(g_value_get_object(&item));
                        GstElementFactory* factory = gst_element_get_factory(elem);
                        if (factory) {
                            const gchar* factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                            if (factoryName && strcmp(factoryName, "udpsrc") == 0) {
                                udpsrcElement_ = elem;
                                gst_object_ref(udpsrcElement_);
                                done = TRUE;
                            }
                        }
                        g_value_reset(&item);
                        break;
                    }
                    case GST_ITERATOR_RESYNC:
                        gst_iterator_resync(it);
                        break;
                    case GST_ITERATOR_ERROR:
                    case GST_ITERATOR_DONE:
                        done = TRUE;
                        break;
                }
            }
            gst_iterator_free(it);
        }
    }

    if (udpsrcElement_) {
        LOGD("GstPlayer::start — udpsrc element found, monitoring bytes-served");
    } else {
        LOGW("GstPlayer::start — could not find udpsrc element in pipeline, "
             "no-data timeout will not work");
    }

    // Record the start time for timeout detection.
    lastDataTime_ = nowSeconds();

    // ── Start the GLib main loop thread ───────────────────────────────
    startMainLoop();

    g_usleep(10000); // 10ms

    if (!mainLoopRunning_) {
        LOGE("GstPlayer::start — GLib main loop failed to start");
        gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline_));
        pipeline_ = nullptr;
        return false;
    }

    // ── Set up bus message watch on the main loop's context ───────────
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));

    GSource* busSource = gst_bus_create_watch(bus);
    g_source_set_callback(busSource,
                          reinterpret_cast<GSourceFunc>(bus_message_callback),
                          this,
                          nullptr);
    g_source_attach(busSource, GST_MAIN_CTX_CAST(mainContext_));
    g_source_unref(busSource);

    busWatchId_ = g_source_get_id(busSource);
    gst_object_unref(GST_OBJECT(bus));

    LOGD("GstPlayer::start — bus watch attached (source id: %u)", busWatchId_);

    // ── Set up periodic timeout check ─────────────────────────────────
    // Fires every 2 seconds on the main loop to check if data is arriving.
    timeoutSourceId_ = g_timeout_add_seconds(
        2,
        timeout_check_callback,
        this
    );

    LOGD("GstPlayer::start — timeout check timer attached (source id: %u)",
         timeoutSourceId_);

    // ── Start the pipeline ────────────────────────────────────────────
    GstStateChangeReturn ret = gst_element_set_state(
        GST_ELEMENT(pipeline_),
        GST_STATE_PLAYING
    );

    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOGE("GstPlayer::start — FAILED to set pipeline to PLAYING");
        gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline_));
        pipeline_ = nullptr;
        stopMainLoop();
        return false;
    }

    running_ = true;
    LOGI("GstPlayer::start — pipeline is PLAYING (state_change_return: %d)", ret);
    return true;
}

void GstPlayer::stop() {
    LOGD("GstPlayer::stop — entering (running=%d, pipeline=%p)", running_, pipeline_);

    // Clean up the timeout timer.
    if (timeoutSourceId_ > 0) {
        g_source_remove(timeoutSourceId_);
        timeoutSourceId_ = 0;
    }

    // Clean up the bus watch.
    if (busWatchId_ > 0) {
        g_source_remove(busWatchId_);
        busWatchId_ = 0;
    }

    // Stop the GLib main loop thread.
    stopMainLoop();

    // Tear down the pipeline if it exists.
    if (pipeline_) {
        LOGI("GstPlayer::stop — transitioning pipeline to NULL");

        GstStateChangeReturn ret = gst_element_set_state(
            GST_ELEMENT(pipeline_),
            GST_STATE_NULL
        );

        if (ret == GST_STATE_CHANGE_ASYNC) {
            GstState state;
            ret = gst_element_get_state(
                GST_ELEMENT(pipeline_),
                &state, nullptr,
                2 * GST_SECOND
            );
            if (ret == GST_STATE_CHANGE_FAILURE) {
                LOGW("GstPlayer::stop — state change to NULL timed out or failed, "
                     "force unref anyway");
            }
        }

        gst_object_unref(GST_OBJECT(pipeline_));
        pipeline_ = nullptr;
    }

    // Release the udpsrc element reference.
    if (udpsrcElement_) {
        gst_object_unref(GST_OBJECT(udpsrcElement_));
        udpsrcElement_ = nullptr;
    }

    // Clear error state.
    hasError_.store(false);
    lastError_.clear();
    lastDataTime_ = 0;

    running_ = false;
    LOGI("GstPlayer::stop — pipeline stopped and cleaned up");
}

void GstPlayer::reset() {
    LOGI("GstPlayer::reset — full reset");

    stop();
    deinit();

    pipelineDesc_.clear();

    LOGI("GstPlayer::reset — player returned to initial state");
}

bool GstPlayer::isPlaying() const {
    return running_;
}

// ── Stream status ─────────────────────────────────────────────────────

bool GstPlayer::hasError() const {
    return hasError_.load();
}

std::string GstPlayer::getLastError() const {
    return lastError_;
}
