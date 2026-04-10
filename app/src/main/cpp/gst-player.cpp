#include "gst-player.h"

#include <android/log.h>
#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

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
    LOGI("GST_DIAG checking parse-only element factories");
    logFactoryDiagnostic("udpsrc");
    logFactoryDiagnostic("rtpjitterbuffer");
    logFactoryDiagnostic("rtph265depay");
    logFactoryDiagnostic("h265parse");
    logFactoryDiagnostic("appsink");
}

static std::string formatHexPrefix(const guint8* data, gsize size, gsize maxBytes = 8) {
    if (!data || size == 0) {
        return "empty";
    }

    char formatted[3 * 8 + 1] = {};
    const gsize prefixSize = std::min(size, maxBytes);
    gsize offset = 0;

    for (gsize i = 0; i < prefixSize && offset < sizeof(formatted); ++i) {
        const int written = std::snprintf(
            formatted + offset,
            sizeof(formatted) - offset,
            i == 0 ? "%02X" : " %02X",
            data[i]);
        if (written <= 0) {
            break;
        }
        offset += static_cast<gsize>(written);
    }

    return formatted;
}

static bool startsWithAnnexBStartCode(const guint8* data, gsize size) {
    if (!data || size < 3) {
        return false;
    }

    if (size >= 4 &&
        data[0] == 0x00 && data[1] == 0x00 &&
        data[2] == 0x00 && data[3] == 0x01) {
        return true;
    }

    return data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01;
}

static const char* hevcNalTypeName(guint8 nalType) {
    switch (nalType) {
        case 19:
            return "IDR_W_RADL";
        case 20:
            return "IDR_N_LP";
        case 21:
            return "CRA";
        case 32:
            return "VPS";
        case 33:
            return "SPS";
        case 34:
            return "PPS";
        case 35:
            return "AUD";
        case 39:
            return "PREFIX_SEI";
        case 40:
            return "SUFFIX_SEI";
        default:
            return "OTHER";
    }
}

static std::string summarizeAnnexBNalTypes(const guint8* data,
                                           gsize size,
                                           bool* sawVps,
                                           bool* sawSps,
                                           bool* sawPps,
                                           guint* nalCount) {
    if (nalCount) {
        *nalCount = 0;
    }
    if (!data || size < 5) {
        return "none";
    }

    std::string summary;
    for (gsize i = 0; i + 3 < size;) {
        gsize nalStart = 0;
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            nalStart = i + 3;
        } else if (i + 4 < size &&
                   data[i] == 0x00 && data[i + 1] == 0x00 &&
                   data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            nalStart = i + 4;
        } else {
            ++i;
            continue;
        }

        if (nalStart >= size) {
            break;
        }

        const guint8 nalType = (data[nalStart] >> 1) & 0x3F;
        if (nalCount) {
            ++(*nalCount);
        }
        if (sawVps && nalType == 32) {
            *sawVps = true;
        }
        if (sawSps && nalType == 33) {
            *sawSps = true;
        }
        if (sawPps && nalType == 34) {
            *sawPps = true;
        }

        if (!summary.empty()) {
            summary += ",";
        }
        summary += hevcNalTypeName(nalType);
        if (summary.size() > 96) {
            summary += ",...";
            break;
        }

        i = nalStart + 1;
    }

    return summary.empty() ? "none" : summary;
}

static void logFactoryEntryList(const char* category,
                                std::vector<std::string> entries) {
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

    if (entries.empty()) {
        LOGW("GST_DIAG category=%s count=0", category);
        return;
    }

    std::string joined;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += entries[i];
    }

    LOGI("GST_DIAG category=%s count=%zu entries=%s",
         category,
         entries.size(),
         joined.c_str());
}

static void logPipelineElementsForParseDebug(GstElement* pipeline) {
    std::vector<std::string> entries;
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline));
    if (!it) {
        LOGW("GST_DIAG pipeline-elements unavailable");
        return;
    }

    gboolean done = FALSE;
    while (!done) {
        GValue item = G_VALUE_INIT;
        switch (gst_iterator_next(it, &item)) {
            case GST_ITERATOR_OK: {
                auto* elem = GST_ELEMENT(g_value_get_object(&item));
                auto* factory = gst_element_get_factory(elem);
                if (factory) {
                    const gchar* factoryName =
                        gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                    const gchar* pluginName =
                        gst_plugin_feature_get_plugin_name(GST_PLUGIN_FEATURE(factory));
                    const gchar* elementName = gst_element_get_name(elem);

                    const bool relevant =
                        gst_element_factory_list_is_type(
                            factory, GST_ELEMENT_FACTORY_TYPE_SINK |
                                     GST_ELEMENT_FACTORY_TYPE_PARSER |
                                     GST_ELEMENT_FACTORY_TYPE_DEPAYLOADER);

                    if (relevant) {
                        std::string entry = elementName ? elementName : "(unnamed)";
                        entry += "=";
                        entry += factoryName ? factoryName : "(unknown)";
                        entry += "(";
                        entry += pluginName ? pluginName : "?";
                        entry += ")";
                        entries.push_back(entry);
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
    logFactoryEntryList("pipeline_relevant_elements", entries);
}

static void logUdpsrcSocketDetails(GstElement* udpsrc) {
    if (!udpsrc) {
        LOGW("GST_UDPSRC socket-details unavailable: null element");
        return;
    }

    gchar* address = nullptr;
    gint port = 0;
    gboolean reuse = FALSE;
    GSocket* usedSocket = nullptr;
    g_object_get(
        G_OBJECT(udpsrc),
        "address", &address,
        "port", &port,
        "reuse", &reuse,
        "used-socket", &usedSocket,
        nullptr);

    LOGI("GST_UDPSRC config address=%s port=%d reuse=%d usedSocket=%p",
         address ? address : "(null)",
         port,
         reuse ? 1 : 0,
         usedSocket);

    if (usedSocket) {
        GError* error = nullptr;
        GSocketAddress* localAddress = g_socket_get_local_address(usedSocket, &error);
        if (!localAddress) {
            LOGW("GST_UDPSRC local-address unavailable: %s",
                 error ? error->message : "unknown");
            if (error) {
                g_error_free(error);
            }
        } else if (G_IS_INET_SOCKET_ADDRESS(localAddress)) {
            auto* inetAddress = g_inet_socket_address_get_address(
                G_INET_SOCKET_ADDRESS(localAddress));
            gchar* ip = g_inet_address_to_string(inetAddress);
            const guint16 boundPort = g_inet_socket_address_get_port(
                G_INET_SOCKET_ADDRESS(localAddress));
            LOGI("GST_UDPSRC bound-local=%s:%u family=%d blocking=%d",
                 ip ? ip : "(null)",
                 boundPort,
                 static_cast<int>(g_socket_get_family(usedSocket)),
                 g_socket_get_blocking(usedSocket) ? 1 : 0);
            g_free(ip);
            g_object_unref(localAddress);
        } else {
            LOGI("GST_UDPSRC local-address type=%s",
                 G_OBJECT_TYPE_NAME(localAddress));
            g_object_unref(localAddress);
        }

        g_object_unref(usedSocket);
    }

    g_free(address);
}

static GSocket* createBoundUdpSocket(guint16 port) {
    GError* error = nullptr;
    GSocket* socket = g_socket_new(
        G_SOCKET_FAMILY_IPV4,
        G_SOCKET_TYPE_DATAGRAM,
        G_SOCKET_PROTOCOL_UDP,
        &error);
    if (!socket) {
        LOGE("GST_UDPSRC custom-socket create failed: %s",
             error ? error->message : "unknown");
        if (error) {
            g_error_free(error);
        }
        return nullptr;
    }

    GInetAddress* anyAddress = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* bindAddress = g_inet_socket_address_new(anyAddress, port);
    g_object_unref(anyAddress);

    if (!g_socket_bind(socket, bindAddress, TRUE, &error)) {
        LOGE("GST_UDPSRC custom-socket bind failed on port=%u: %s",
             port,
             error ? error->message : "unknown");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(bindAddress);
        g_object_unref(socket);
        return nullptr;
    }

    g_object_unref(bindAddress);
    LOGI("GST_UDPSRC custom-socket bound on 0.0.0.0:%u socket=%p", port, socket);
    return socket;
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

    LOGI("GST_DIAG expecting sdk static bootstrap during gst_init_check");

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
    resetSampleStats();

    auto* appSink = GST_APP_SINK(
        gst_bin_get_by_name(GST_BIN(pipeline_), "hevcappsink"));
    if (!appSink) {
        LOGE("GstPlayer::buildPipeline — could not find element 'hevcappsink' in pipeline");
        logPipelineElementsForParseDebug(GST_ELEMENT(pipeline_));
        gst_object_unref(GST_OBJECT(pipeline_));
        pipeline_ = nullptr;
        return false;
    }

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &GstPlayer::onNewSampleThunk;
    gst_app_sink_set_callbacks(appSink, &callbacks, this, nullptr);
    appSinkElement_ = appSink;

    LOGI("GstPlayer::buildPipeline — pipeline built successfully");
    logPipelineElementsForParseDebug(GST_ELEMENT(pipeline_));
    return true;
}

void GstPlayer::resetSampleStats() {
    sampleCount_.store(0);
    sampleBytes_.store(0);
    sawVps_ = false;
    sawSps_ = false;
    sawPps_ = false;
}

GstFlowReturn GstPlayer::onNewSampleThunk(GstAppSink* sink, gpointer userData) {
    auto* player = static_cast<GstPlayer*>(userData);
    return player ? player->onNewSample(sink) : GST_FLOW_ERROR;
}

GstFlowReturn GstPlayer::onNewSample(GstAppSink* sink) {
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        LOGW("GST_APPSINK sample pull returned null");
        return GST_FLOW_ERROR;
    }

    const uint64_t sampleIndex = sampleCount_.fetch_add(1) + 1;
    GstCaps* caps = gst_sample_get_caps(sample);
    if (sampleIndex == 1 && caps) {
        gchar* capsString = gst_caps_to_string(caps);
        const GstStructure* capsStruct = gst_caps_get_structure(caps, 0);
        const gchar* streamFormat =
            capsStruct ? gst_structure_get_string(capsStruct, "stream-format") : nullptr;
        const gchar* alignment =
            capsStruct ? gst_structure_get_string(capsStruct, "alignment") : nullptr;
        LOGI("GST_APPSINK first-sample caps=%s stream-format=%s alignment=%s",
             capsString ? capsString : "(null)",
             streamFormat ? streamFormat : "(unset)",
             alignment ? alignment : "(unset)");
        g_free(capsString);
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOGW("GST_APPSINK sample=%llu has no buffer",
             static_cast<unsigned long long>(sampleIndex));
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map = {};
    const bool mapped = gst_buffer_map(buffer, &map, GST_MAP_READ);
    const guint size = mapped ? static_cast<guint>(map.size) : gst_buffer_get_size(buffer);
    sampleBytes_.fetch_add(size);

    const guint64 pts =
        GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer) / 1000 : GST_CLOCK_TIME_NONE;
    const guint64 dts =
        GST_BUFFER_DTS_IS_VALID(buffer) ? GST_BUFFER_DTS(buffer) / 1000 : GST_CLOCK_TIME_NONE;
    const bool isDelta = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    const bool isHeader = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER);
    const bool isDiscont = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT);
    const bool hasAnnexB = mapped && startsWithAnnexBStartCode(map.data, map.size);
    guint nalCount = 0;
    const std::string nalSummary = hasAnnexB
        ? summarizeAnnexBNalTypes(map.data, map.size, &sawVps_, &sawSps_, &sawPps_, &nalCount)
        : "non-annexb";

    if (sampleIndex <= 10 || sampleIndex % 60 == 0) {
        LOGI("GST_APPSINK sample=%llu size=%u ptsUs=%s%llu dtsUs=%s%llu keyframe=%d header=%d discont=%d annexb=%d nalCount=%u nals=%s seenVps=%d seenSps=%d seenPps=%d prefix=%s totalBytes=%llu",
             static_cast<unsigned long long>(sampleIndex),
             size,
             pts == GST_CLOCK_TIME_NONE ? "invalid:" : "",
             pts == GST_CLOCK_TIME_NONE ? 0ULL : static_cast<unsigned long long>(pts),
             dts == GST_CLOCK_TIME_NONE ? "invalid:" : "",
             dts == GST_CLOCK_TIME_NONE ? 0ULL : static_cast<unsigned long long>(dts),
             isDelta ? 0 : 1,
             isHeader ? 1 : 0,
             isDiscont ? 1 : 0,
             hasAnnexB ? 1 : 0,
             nalCount,
             nalSummary.c_str(),
             sawVps_ ? 1 : 0,
             sawSps_ ? 1 : 0,
             sawPps_ ? 1 : 0,
             mapped ? formatHexPrefix(map.data, map.size).c_str() : "unmapped",
             static_cast<unsigned long long>(sampleBytes_.load()));
    }

    if (mapped) {
        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

bool GstPlayer::configureUdpsrcSocket(GstElement* udpsrc) {
    if (!udpsrc) {
        LOGE("GST_UDPSRC configure failed: null udpsrc");
        return false;
    }

    guint port = 0;
    g_object_get(G_OBJECT(udpsrc), "port", &port, nullptr);
    if (port == 0) {
        LOGE("GST_UDPSRC configure failed: invalid port=0");
        return false;
    }

    if (externalUdpSocket_) {
        g_object_unref(G_OBJECT(externalUdpSocket_));
        externalUdpSocket_ = nullptr;
    }

    GSocket* socket = createBoundUdpSocket(static_cast<guint16>(port));
    if (!socket) {
        return false;
    }

    g_object_set(
        G_OBJECT(udpsrc),
        "socket", socket,
        "close-socket", FALSE,
        nullptr);
    externalUdpSocket_ = socket;

    LOGI("GST_UDPSRC custom-socket attached to udpsrc port=%u", port);
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

    LOGI("GstPlayer::start — parse-only pipeline active; ANativeWindow %p reserved for MediaCodec stage",
         window);

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
        if (!configureUdpsrcSocket(GST_ELEMENT(udpsrcElement_))) {
            LOGE("GstPlayer::start — failed to configure custom udpsrc socket");
            gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
            gst_object_unref(GST_OBJECT(pipeline_));
            pipeline_ = nullptr;
            if (udpsrcElement_) {
                gst_object_unref(GST_OBJECT(udpsrcElement_));
                udpsrcElement_ = nullptr;
            }
            return false;
        }
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
    GSource* timeoutSource = g_timeout_source_new_seconds(2);
    g_source_set_callback(
        timeoutSource,
        reinterpret_cast<GSourceFunc>(timeout_check_callback),
        this,
        nullptr);
    timeoutSourceId_ = g_source_attach(timeoutSource, GST_MAIN_CTX_CAST(mainContext_));
    g_source_unref(timeoutSource);

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

    GstState currentState = GST_STATE_NULL;
    GstState pendingState = GST_STATE_NULL;
    const GstStateChangeReturn stateQuery = gst_element_get_state(
        GST_ELEMENT(pipeline_),
        &currentState,
        &pendingState,
        250 * GST_MSECOND);
    LOGI("GstPlayer::start — get_state return=%d current=%s pending=%s",
         stateQuery,
         gst_element_state_get_name(currentState),
         gst_element_state_get_name(pendingState));
    if (udpsrcElement_) {
        logUdpsrcSocketDetails(GST_ELEMENT(udpsrcElement_));
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

    if (appSinkElement_) {
        gst_object_unref(GST_OBJECT(appSinkElement_));
        appSinkElement_ = nullptr;
    }

    if (externalUdpSocket_) {
        g_object_unref(G_OBJECT(externalUdpSocket_));
        externalUdpSocket_ = nullptr;
    }

    // Clear error state.
    hasError_.store(false);
    lastError_.clear();
    lastDataTime_ = 0;
    resetSampleStats();

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
