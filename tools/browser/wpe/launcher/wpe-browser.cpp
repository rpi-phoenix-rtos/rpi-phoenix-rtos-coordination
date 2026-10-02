/*
 * wpe-browser: the WPE WebKit browser of Phoenix-RTOS (browser plan, milestones B4/B5).
 *
 * One static multi-call program (plan decision 2). Phoenix has no shared libraries, so the UI
 * process, the WebProcess and the NetworkProcess are the same ELF: WebKit's ProcessLauncher
 * (patch 0007) execs WPE_PHOENIX_EXECUTABLE -- this program, as recorded below -- with the
 * child's role in WPE_PHOENIX_PROCESS_ROLE and WebKit's usual argv (<identifier> <socket>).
 *
 * UI role, a minimal shell on WPEPlatform:
 *   wpe-browser [options] [URL|FILE]
 *     --headless            no window: WPEPlatform's headless display (B4)
 *     --snapshot=FILE.png   after the first load finishes, save a snapshot of the visible
 *                           area as PNG, print its checksum and exit
 *     --size=WxH            view size (default 1024x768; Wayland: the initial window size)
 *     --timeout=S           exit with status 2 if the page has not finished loading after S s
 *     --exit-after-load     exit once the first load has finished (window mode)
 *     --ignore-tls-errors   accept invalid certificates
 *     --cpu-rendering       paint with Skia's CPU raster (WEBKIT_SKIA_ENABLE_CPU_RENDERING=1)
 *   Keys (window): Ctrl+Q quit, Ctrl+R / F5 reload, Alt+Left / Alt+Right back / forward,
 *   Alt+Home the start page, F11 fullscreen.
 * Every line this program prints starts with "WPEB " (UART-friendly, one event per line).
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "cmakeconfig.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib-unix.h>
#include <png.h>
#include <wpe/webkit.h>
#include <wpe/wpe-platform.h>
#if ENABLE_WPE_PLATFORM_HEADLESS
#include <wpe/headless/wpe-headless.h>
#endif
#if ENABLE_WPE_PLATFORM_WAYLAND
#include <wpe/wayland/wpe-wayland.h>
#endif

namespace WebKit {
int WebProcessMain(int argc, char** argv);
int NetworkProcessMain(int argc, char** argv);
}

/* glib-networking's OpenSSL backend, linked statically: its module entry point registers the
 * GTlsBackend (a static program has no GIO module loading). */
extern "C" void g_io_openssl_load(GIOModule* module);

static double nowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static double startMs;

#define LOG(...)                                                    \
    do {                                                            \
        fprintf(stderr, "WPEB t=%.0f ", nowMs() - startMs);         \
        fprintf(stderr, __VA_ARGS__);                               \
        fputc('\n', stderr);                                        \
    } while (0)

/* --- roles ---------------------------------------------------------------------------------- */

/* The absolute path of this executable, for the children (WebKit execs it by path). */
static void recordExecutablePath(const char* argv0)
{
    if (getenv("WPE_PHOENIX_EXECUTABLE"))
        return;
    char resolved[PATH_MAX];
    if (strchr(argv0, '/')) {
        if (realpath(argv0, resolved))
            setenv("WPE_PHOENIX_EXECUTABLE", resolved, 1);
        return;
    }
    /* a bare name: the first PATH entry holding it */
    const char* path = getenv("PATH");
    if (!path)
        path = "/bin:/usr/bin";
    char* copy = strdup(path);
    for (char* dir = strtok(copy, ":"); dir; dir = strtok(nullptr, ":")) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, argv0);
        if (access(candidate, X_OK) == 0 && realpath(candidate, resolved)) {
            setenv("WPE_PHOENIX_EXECUTABLE", resolved, 1);
            break;
        }
    }
    free(copy);
}

/* --- UI role: options ----------------------------------------------------------------------- */

static gboolean optHeadless;
static char* optSnapshot;
static char* optSize;
static int optTimeout;
static gboolean optExitAfterLoad;
static gboolean optIgnoreTLSErrors;
static gboolean optCPURendering;
static char** optURIs;

static const GOptionEntry optionEntries[] = {
    { "headless", 0, 0, G_OPTION_ARG_NONE, &optHeadless, "No window (WPEPlatform headless display)", nullptr },
    { "snapshot", 0, 0, G_OPTION_ARG_FILENAME, &optSnapshot, "Save a PNG snapshot after the first load and exit", "FILE" },
    { "size", 0, 0, G_OPTION_ARG_STRING, &optSize, "View size", "WxH" },
    { "timeout", 0, 0, G_OPTION_ARG_INT, &optTimeout, "Fail (exit 2) if the first load takes longer", "S" },
    { "exit-after-load", 0, 0, G_OPTION_ARG_NONE, &optExitAfterLoad, "Exit when the first load has finished", nullptr },
    { "ignore-tls-errors", 0, 0, G_OPTION_ARG_NONE, &optIgnoreTLSErrors, "Accept invalid TLS certificates", nullptr },
    { "cpu-rendering", 0, 0, G_OPTION_ARG_NONE, &optCPURendering, "Skia CPU raster in the WebProcess", nullptr },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &optURIs, nullptr, "[URL|FILE]" },
    { }
};

/* --- UI role: state ------------------------------------------------------------------------- */

static GMainLoop* mainLoop;
static int exitStatus;
static gboolean firstLoadDone;
static char* homeURI;

static void quit(int status)
{
    exitStatus = status;
    g_main_loop_quit(mainLoop);
}

static guint32 crc32Update(guint32 crc, const guint8* data, gsize length)
{
    crc = ~crc;
    for (gsize i = 0; i < length; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

/* WebKitImage: 32-bit BGRA (little-endian), premultiplied alpha. PNG wants RGBA, straight. */
static bool writePNG(const char* path, WebKitImage* image, guint32* crcOut)
{
    int width = webkit_image_get_width(image);
    int height = webkit_image_get_height(image);
    guint stride = webkit_image_get_stride(image);
    GBytes* bytes = webkit_image_as_bytes(image);
    gsize size;
    const guint8* pixels = static_cast<const guint8*>(g_bytes_get_data(bytes, &size));

    FILE* file = fopen(path, "wb");
    if (!file) {
        LOG("snapshot-error open %s: %s", path, strerror(errno));
        return false;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    guint8* row = static_cast<guint8*>(g_malloc(static_cast<gsize>(width) * 4));
    guint32 crc = 0;
    bool ok = false;
    if (png && info && !setjmp(png_jmpbuf(png))) {
        png_init_io(png, file);
        png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        for (int y = 0; y < height; y++) {
            const guint8* src = pixels + static_cast<gsize>(y) * stride;
            for (int x = 0; x < width; x++) {
                guint8 b = src[4 * x], g = src[4 * x + 1], r = src[4 * x + 2], a = src[4 * x + 3];
                if (a && a != 255) {
                    r = static_cast<guint8>(r * 255 / a);
                    g = static_cast<guint8>(g * 255 / a);
                    b = static_cast<guint8>(b * 255 / a);
                }
                row[4 * x] = r;
                row[4 * x + 1] = g;
                row[4 * x + 2] = b;
                row[4 * x + 3] = a;
            }
            crc = crc32Update(crc, row, static_cast<gsize>(width) * 4);
            png_write_row(png, row);
        }
        png_write_end(png, nullptr);
        ok = true;
    }
    png_destroy_write_struct(&png, &info);
    g_free(row);
    fclose(file);
    g_bytes_unref(bytes);
    *crcOut = crc;
    return ok;
}

static void snapshotReady(GObject* object, GAsyncResult* result, gpointer)
{
    GError* error = nullptr;
    WebKitImage* image = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(object), result, &error);
    if (!image) {
        LOG("snapshot-error %s", error ? error->message : "unknown");
        g_clear_error(&error);
        quit(1);
        return;
    }
    guint32 crc = 0;
    bool ok = writePNG(optSnapshot, image, &crc);
    if (ok)
        LOG("snapshot file=%s width=%d height=%d crc32=%08x", optSnapshot, webkit_image_get_width(image),
            webkit_image_get_height(image), crc);
    g_object_unref(image);
    quit(ok ? 0 : 1);
}

static const char* loadEventName(WebKitLoadEvent event)
{
    switch (event) {
    case WEBKIT_LOAD_STARTED:
        return "started";
    case WEBKIT_LOAD_REDIRECTED:
        return "redirected";
    case WEBKIT_LOAD_COMMITTED:
        return "committed";
    case WEBKIT_LOAD_FINISHED:
        return "finished";
    }
    return "?";
}

static void loadChanged(WebKitWebView* webView, WebKitLoadEvent event, gpointer)
{
    LOG("load %s uri=%s", loadEventName(event), webkit_web_view_get_uri(webView));
    if (event != WEBKIT_LOAD_FINISHED || firstLoadDone)
        return;
    firstLoadDone = TRUE;
    if (optSnapshot) {
        webkit_web_view_get_snapshot(webView, WEBKIT_SNAPSHOT_REGION_VISIBLE, WEBKIT_SNAPSHOT_OPTIONS_NONE,
            nullptr, snapshotReady, nullptr);
        return;
    }
    if (optExitAfterLoad)
        quit(0);
}

static gboolean loadFailed(WebKitWebView*, WebKitLoadEvent, const char* uri, GError* error, gpointer)
{
    LOG("load-failed uri=%s error=%s", uri, error ? error->message : "?");
    return FALSE; /* WebKit's error page */
}

static gboolean loadFailedTLS(WebKitWebView*, const char* uri, GTlsCertificate*, GTlsCertificateFlags errors, gpointer)
{
    LOG("load-failed-tls uri=%s flags=0x%x", uri, static_cast<unsigned>(errors));
    return FALSE;
}

static void webProcessTerminated(WebKitWebView*, WebKitWebProcessTerminationReason reason, gpointer)
{
    LOG("web-process-terminated reason=%s", reason == WEBKIT_WEB_PROCESS_CRASHED ? "crashed"
        : reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT ? "memory-limit" : "api");
    if (optSnapshot || optExitAfterLoad)
        quit(3);
}

static void titleChanged(WebKitWebView* webView, GParamSpec*, gpointer)
{
    const char* title = webkit_web_view_get_title(webView);
    LOG("title %s", title ? title : "");
    if (WPEView* view = webkit_web_view_get_wpe_view(webView)) {
        if (WPEToplevel* toplevel = wpe_view_get_toplevel(view))
            wpe_toplevel_set_title(toplevel, title && *title ? title : "wpe-browser");
    }
}

static void progressChanged(WebKitWebView* webView, GParamSpec*, gpointer)
{
    LOG("progress %.2f", webkit_web_view_get_estimated_load_progress(webView));
}

static gboolean viewEvent(WPEView* view, WPEEvent* event, WebKitWebView* webView)
{
    if (wpe_event_get_event_type(event) != WPE_EVENT_KEYBOARD_KEY_DOWN)
        return FALSE;
    WPEModifiers modifiers = wpe_event_get_modifiers(event);
    guint keyval = wpe_event_keyboard_get_keyval(event);
    if (modifiers & WPE_MODIFIER_KEYBOARD_CONTROL) {
        if (keyval == WPE_KEY_q || keyval == WPE_KEY_Q) {
            LOG("key quit");
            quit(0);
            return TRUE;
        }
        if (keyval == WPE_KEY_r || keyval == WPE_KEY_R) {
            LOG("key reload");
            webkit_web_view_reload(webView);
            return TRUE;
        }
    }
    if (keyval == WPE_KEY_F5) {
        LOG("key reload");
        webkit_web_view_reload(webView);
        return TRUE;
    }
    if (modifiers & WPE_MODIFIER_KEYBOARD_ALT) {
        if ((keyval == WPE_KEY_Left || keyval == WPE_KEY_KP_Left) && webkit_web_view_can_go_back(webView)) {
            LOG("key back");
            webkit_web_view_go_back(webView);
            return TRUE;
        }
        if ((keyval == WPE_KEY_Right || keyval == WPE_KEY_KP_Right) && webkit_web_view_can_go_forward(webView)) {
            LOG("key forward");
            webkit_web_view_go_forward(webView);
            return TRUE;
        }
        if (keyval == WPE_KEY_Home) {
            LOG("key home");
            webkit_web_view_load_uri(webView, homeURI);
            return TRUE;
        }
    }
    if (keyval == WPE_KEY_F11) {
        if (WPEToplevel* toplevel = wpe_view_get_toplevel(view)) {
            if (wpe_toplevel_get_state(toplevel) & WPE_TOPLEVEL_STATE_FULLSCREEN)
                wpe_toplevel_unfullscreen(toplevel);
            else
                wpe_toplevel_fullscreen(toplevel);
        }
        return TRUE;
    }
    return FALSE;
}

static void displayDisconnected(WPEDisplay*, GError* error, gpointer)
{
    LOG("display-disconnected %s", error ? error->message : "");
    quit(1);
}

static gboolean loadTimeout(gpointer)
{
    if (!firstLoadDone) {
        LOG("timeout after %d s", optTimeout);
        quit(2);
    }
    return G_SOURCE_REMOVE;
}

static gboolean quitOnSignal(gpointer)
{
    LOG("signal quit");
    quit(0);
    return G_SOURCE_REMOVE;
}

static int uiMain(int argc, char** argv)
{
    GOptionContext* context = g_option_context_new("[URL|FILE]");
    g_option_context_add_main_entries(context, optionEntries, nullptr);
    GError* error = nullptr;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "wpe-browser: %s\n", error->message);
        g_option_context_free(context);
        return 1;
    }
    g_option_context_free(context);

    int width = 1024, height = 768;
    if (optSize && sscanf(optSize, "%dx%d", &width, &height) != 2) {
        fprintf(stderr, "wpe-browser: --size wants WxH\n");
        return 1;
    }
    if (optCPURendering)
        g_setenv("WEBKIT_SKIA_ENABLE_CPU_RENDERING", "1", TRUE); /* inherited by the WebProcess */

    const char* arg = (optURIs && optURIs[0]) ? optURIs[0] : "about:blank";
    GFile* file = g_file_new_for_commandline_arg(arg);
    homeURI = strstr(arg, "://") || g_str_has_prefix(arg, "about:") ? g_strdup(arg) : g_file_get_uri(file);
    g_object_unref(file);

    LOG("start pid=%d webkit=%u.%u.%u mode=%s uri=%s exe=%s", static_cast<int>(getpid()), webkit_get_major_version(),
        webkit_get_minor_version(), webkit_get_micro_version(), optHeadless ? "headless" : "window", homeURI,
        getenv("WPE_PHOENIX_EXECUTABLE"));

    mainLoop = g_main_loop_new(nullptr, FALSE);

    WPEDisplay* display = nullptr;
    if (optHeadless) {
#if ENABLE_WPE_PLATFORM_HEADLESS
        display = wpe_display_headless_new();
#endif
    } else {
#if ENABLE_WPE_PLATFORM_WAYLAND
        display = wpe_display_wayland_new();
#endif
    }
    if (!display) {
        LOG("no WPE display for mode %s in this build", optHeadless ? "headless" : "window");
        return 1;
    }
    if (!wpe_display_connect(display, &error)) {
        LOG("display-connect-failed %s (WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s)", error ? error->message : "?",
            g_getenv("WAYLAND_DISPLAY"), g_getenv("XDG_RUNTIME_DIR"));
        g_clear_error(&error);
        return 1;
    }
    LOG("display %s", G_OBJECT_TYPE_NAME(display));
    g_signal_connect(display, "disconnected", G_CALLBACK(displayDisconnected), nullptr);

    WebKitNetworkSession* session = webkit_network_session_new_ephemeral();
    if (optIgnoreTLSErrors)
        webkit_network_session_set_tls_errors_policy(session, WEBKIT_TLS_ERRORS_POLICY_IGNORE);
    WebKitSettings* settings = webkit_settings_new_with_settings(
        "enable-webgl", FALSE,
        "enable-media", FALSE,
        "enable-webaudio", FALSE,
        "enable-developer-extras", FALSE,
        "enable-page-cache", FALSE,
        "enable-2d-canvas-acceleration", FALSE,
        "enable-write-console-messages-to-stdout", TRUE,
        nullptr);

    WebKitWebView* webView = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "display", display,
        "network-session", session,
        "settings", settings,
        nullptr));
    g_object_unref(settings);

    g_signal_connect(webView, "load-changed", G_CALLBACK(loadChanged), nullptr);
    g_signal_connect(webView, "load-failed", G_CALLBACK(loadFailed), nullptr);
    g_signal_connect(webView, "load-failed-with-tls-errors", G_CALLBACK(loadFailedTLS), nullptr);
    g_signal_connect(webView, "web-process-terminated", G_CALLBACK(webProcessTerminated), nullptr);
    g_signal_connect(webView, "notify::title", G_CALLBACK(titleChanged), nullptr);
    g_signal_connect(webView, "notify::estimated-load-progress", G_CALLBACK(progressChanged), nullptr);

    if (WPEView* view = webkit_web_view_get_wpe_view(webView)) {
        g_signal_connect(view, "event", G_CALLBACK(viewEvent), webView);
        if (WPEToplevel* toplevel = wpe_view_get_toplevel(view)) {
            wpe_toplevel_resize(toplevel, width, height);
            wpe_toplevel_set_title(toplevel, "wpe-browser");
        }
        LOG("view %s %dx%d", G_OBJECT_TYPE_NAME(view), width, height);
    }

    if (optTimeout > 0)
        g_timeout_add_seconds(optTimeout, loadTimeout, nullptr);
    g_unix_signal_add(SIGINT, quitOnSignal, nullptr);
    g_unix_signal_add(SIGTERM, quitOnSignal, nullptr);

    webkit_web_view_load_uri(webView, homeURI);
    g_main_loop_run(mainLoop);

    LOG("exit status=%d", exitStatus);
    g_object_unref(webView);
    g_object_unref(session);
    g_object_unref(display);
    g_main_loop_unref(mainLoop);
    return exitStatus;
}

int main(int argc, char** argv)
{
    startMs = nowMs();
    setvbuf(stderr, nullptr, _IOLBF, 0);
    recordExecutablePath(argv[0]);

    /* Every role may open TLS connections through GIO (the NetworkProcess certainly does). */
    g_io_openssl_load(nullptr);

    const char* role = getenv("WPE_PHOENIX_PROCESS_ROLE");
    if (role && *role) {
        char roleCopy[16];
        snprintf(roleCopy, sizeof(roleCopy), "%s", role);
        unsetenv("WPE_PHOENIX_PROCESS_ROLE"); /* not for this process's own children */
        LOG("role=%s pid=%d argc=%d", roleCopy, static_cast<int>(getpid()), argc);
        if (!strcmp(roleCopy, "web"))
            return WebKit::WebProcessMain(argc, argv);
        if (!strcmp(roleCopy, "network"))
            return WebKit::NetworkProcessMain(argc, argv);
        LOG("unknown role %s", roleCopy);
        return 1;
    }
    return uiMain(argc, argv);
}
