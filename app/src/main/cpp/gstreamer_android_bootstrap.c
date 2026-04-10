#include <android/log.h>
#include <gio/gio.h>
#include <gst/gst.h>

#define LOG_TAG "GstPlayerNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define GST_G_IO_MODULE_DECLARE(name) \
extern void G_PASTE(g_io_, G_PASTE(name, _load)) (gpointer data)

#define GST_G_IO_MODULE_LOAD(name) \
G_PASTE(g_io_, G_PASTE(name, _load)) (NULL)

/*
 * SDK-style Android static bootstrap:
 * this mirrors the GStreamer Android template that provides
 * gst_init_static_plugins(), which gst_init() calls during startup.
 */
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(udp);
GST_PLUGIN_STATIC_DECLARE(rtp);
GST_PLUGIN_STATIC_DECLARE(rtpmanager);
GST_PLUGIN_STATIC_DECLARE(videoparsersbad);
GST_PLUGIN_STATIC_DECLARE(isomp4);
GST_PLUGIN_STATIC_DECLARE(androidmedia);
GST_PLUGIN_STATIC_DECLARE(videoconvertscale);
GST_PLUGIN_STATIC_DECLARE(playback);

static void
gst_android_load_gio_modules(void)
{
    GTlsBackend* backend;
    const gchar* ca_certs;

    ca_certs = g_getenv("CA_CERTIFICATES");

    backend = g_tls_backend_get_default();
    if (backend && ca_certs) {
        GTlsDatabase* db;
        GError* error = NULL;

        db = g_tls_file_database_new(ca_certs, &error);
        if (db) {
            g_tls_backend_set_default_database(backend, db);
            g_object_unref(db);
        } else {
            g_warning("Failed to create a database from file: %s",
                      error ? error->message : "Unknown");
        }
    }
}

/*
 * This is called by gst_init()/gst_init_check() when present in the binary.
 */
void
gst_init_static_plugins(void)
{
    LOGI("GST_DIAG sdk static bootstrap starting");

    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(udp);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
    GST_PLUGIN_STATIC_REGISTER(videoparsersbad);
    GST_PLUGIN_STATIC_REGISTER(isomp4);
    GST_PLUGIN_STATIC_REGISTER(androidmedia);
    GST_PLUGIN_STATIC_REGISTER(videoconvertscale);
    GST_PLUGIN_STATIC_REGISTER(playback);

    gst_android_load_gio_modules();

    LOGI("GST_DIAG sdk static bootstrap finished");
}
