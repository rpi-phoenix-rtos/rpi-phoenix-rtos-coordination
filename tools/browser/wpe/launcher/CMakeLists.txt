# wpe-browser: the static multi-call WPE WebKit program of Phoenix-RTOS (UI + WebProcess +
# NetworkProcess in one ELF). Added to the WebKit build by patch 0006 through
# -DPHOENIX_BROWSER_DIR=<this directory>; modelled on Tools/MiniBrowser/wpe/CMakeLists.txt.
#
# PHOENIX_BROWSER_EXTRA_LIBS (set by tools/browser/wpe/build.sh): what a static program needs
# beyond WebKit's own link interface -- Mesa's EGL/GLES archives (epoxy resolves through
# eglGetProcAddress), glib-networking's OpenSSL module, the wayland_phoenix compat library
# (memfd_create over shmsrv) and the --wrap options those libraries are built for.

set(WPEBrowser_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/wpe-browser.cpp
)

set(WPEBrowser_PRIVATE_INCLUDE_DIRECTORIES
    ${CMAKE_BINARY_DIR}
    ${DERIVED_SOURCES_WPE_API_DIR}
    ${FORWARDING_HEADERS_WPE_DIR}
    ${JavaScriptCoreGLib_DERIVED_SOURCES_DIR}
    ${JavaScriptCoreGLib_FRAMEWORK_HEADERS_DIR}
    ${WPEPlatform_DERIVED_SOURCES_DIR}
    ${WEBKIT_DIR}/WPEPlatform
)

set(WPEBrowser_PRIVATE_LIBRARIES
    ${JavaScriptCore_LIBRARY_NAME}
    WebKit::WebKit
    PNG::PNG
    ${PHOENIX_BROWSER_EXTRA_LIBS}
)

set(WPEBrowser_OUTPUT_NAME wpe-browser)

WEBKIT_EXECUTABLE_DECLARE(WPEBrowser)
WEBKIT_EXECUTABLE(WPEBrowser)

install(TARGETS WPEBrowser DESTINATION "${EXEC_INSTALL_DIR}")
