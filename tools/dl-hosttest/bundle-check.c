/*
 * dl-hosttest: load WPE's injected bundle (libWPEInjectedBundle.so) the way the
 * WebProcess does, and call its WKBundleInitialize, against a mock of the three
 * symbols the bundle imports from wpe-browser (bundle-check.exports).
 *
 *   bundle-check <path/to/libWPEInjectedBundle.so> [<path/to/extension.so>]
 *
 * Prints BUNDLE-CHECK PASS when dlopen() bound every import to the mock, dlsym()
 * found WKBundleInitialize, and the call reached WebProcessExtensionManager::initialize
 * with the bundle and the user data it was given.
 *
 * With an extension (tools/browser/wpe/pi/phx-probe-extension.so), the mock initialize
 * then does what WebProcessExtensionManager::initialize does for each module in the
 * extensions directory: dlopen() it and call its
 * webkit_web_process_extension_initialize_with_user_data. The extension's GLib and WebKit
 * imports are mocked too, and its page-created handler is run for one page.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dlfcn.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* WebKit::InjectedBundle as WKBundleInitialize sees it: toImpl() checks the API object's
   type through the third virtual function, and requires API::Object::Type::Bundle */
#define API_TYPE_BUNDLE 0x7b

struct mockObject {
	const uintptr_t *vtable;
};

static int mockType(const struct mockObject *self)
{
	(void)self;
	return API_TYPE_BUNDLE;
}

static const uintptr_t mockVtable[3] = { 0, 0, (uintptr_t)mockType };
static struct mockObject mockBundle = { mockVtable };
static int mockUserData;

static int manager; /* the WebProcessExtensionManager singleton */
static void *gotBundle, *gotUserData;
static int calls;

static const char *extensionPath;
static int extensionObject; /* the WebKitWebProcessExtension */
static int extensionOk;


/* --- the extension's imports --- */

static void (*pageCreated)(void *extension, void *page, void *data);
static int mockPage;

void g_printerr(const char *format, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	(void)fprintf(stderr, "%s", buf);
}


const char *g_variant_get_string(void *value, size_t *length)
{
	(void)length;
	return (const char *)value; /* the mock GVariant is the string */
}


unsigned long g_signal_connect_data(void *instance, const char *signal, void (*handler)(void), void *data,
	void (*destroy)(void *, void *), int flags)
{
	(void)data;
	(void)destroy;
	(void)flags;
	if ((instance == &extensionObject) && (strcmp(signal, "page-created") == 0)) {
		pageCreated = (void (*)(void *, void *, void *))(uintptr_t)handler;
	}
	return 1;
}


uint64_t webkit_web_page_get_id(void *page)
{
	return (page == &mockPage) ? 7 : 0;
}


/* WebProcessExtensionManager::initialize: load one extension module */
static void loadExtension(void)
{
	void *h = dlopen(extensionPath, RTLD_LAZY | RTLD_LOCAL);
	void (*init)(void *, void *);

	if (h == NULL) {
		printf("BUNDLE-CHECK FAIL extension dlopen: %s\n", dlerror());
		return;
	}
	init = (void (*)(void *, void *))(uintptr_t)dlsym(h, "webkit_web_process_extension_initialize_with_user_data");
	if (init == NULL) {
		printf("BUNDLE-CHECK FAIL extension dlsym: %s\n", dlerror());
		return;
	}
	init(&extensionObject, (void *)"wpe-browser");
	if (pageCreated == NULL) {
		printf("BUNDLE-CHECK FAIL extension connected no page-created handler\n");
		return;
	}
	pageCreated(&extensionObject, &mockPage, NULL);
	extensionOk = 1;
}


/* WebKit::WebProcessExtensionManager::singleton() */
void *mock_singleton(void) __asm__("_ZN6WebKit26WebProcessExtensionManager9singletonEv");
void *mock_singleton(void)
{
	return &manager;
}


/* WebKit::WebProcessExtensionManager::initialize(WebKit::InjectedBundle *, API::Object *) */
void mock_initialize(void *self, void *bundle, void *userData) __asm__("_ZN6WebKit26WebProcessExtensionManager10initializeEPNS_14InjectedBundleEPN3API6ObjectE");
void mock_initialize(void *self, void *bundle, void *userData)
{
	if (self == &manager) {
		gotBundle = bundle;
		gotUserData = userData;
		calls++;
		if (extensionPath != NULL) {
			loadExtension();
		}
	}
}


int main(int argc, char *argv[])
{
	void *h;
	void (*init)(void *, void *);

	if ((argc != 2) && (argc != 3)) {
		printf("usage: bundle-check <libWPEInjectedBundle.so> [<extension.so>]\n");
		return 2;
	}
	extensionPath = (argc == 3) ? argv[2] : NULL;
	h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (h == NULL) {
		printf("BUNDLE-CHECK FAIL dlopen: %s\n", dlerror());
		return 1;
	}
	init = (void (*)(void *, void *))(uintptr_t)dlsym(h, "WKBundleInitialize");
	if (init == NULL) {
		printf("BUNDLE-CHECK FAIL dlsym: %s\n", dlerror());
		return 1;
	}
	init(&mockBundle, &mockUserData);
	if ((calls != 1) || (gotBundle != &mockBundle) || (gotUserData != &mockUserData)) {
		printf("BUNDLE-CHECK FAIL initialize calls=%d bundle=%p userData=%p\n", calls, gotBundle, gotUserData);
		return 1;
	}
	if ((extensionPath != NULL) && (extensionOk == 0)) {
		return 1;
	}
	printf("BUNDLE-CHECK PASS WKBundleInitialize reached WebProcessExtensionManager::initialize%s\n",
		(extensionPath != NULL) ? ", which loaded the extension" : "");
	return 0;
}
