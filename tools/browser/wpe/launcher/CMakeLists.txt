# wpe-browser: the static multi-call WPE WebKit program of Phoenix-RTOS (UI + WebProcess +
# NetworkProcess in one ELF). Added to the WebKit build by patch 0006 through
# -DPHOENIX_BROWSER_DIR=<this directory>; modelled on Tools/MiniBrowser/wpe/CMakeLists.txt.
#
# What a static program needs beyond WebKit's own link interface -- Mesa's EGL/GLES archives
# (epoxy resolves through eglGetProcAddress), glib-networking's OpenSSL module, the
# wayland_phoenix compat library (memfd_create over shmsrv), the static closure of every
# dependency and the --wrap options -- is appended to every C++ link by tools/browser/wpe/build.sh
# (CMAKE_CXX_STANDARD_LIBRARIES), so that it comes after all of WebCore's libraries.

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

# libWPEWebKit is static on Phoenix (patch 0006): the program links the frameworks it is made
# of (bmalloc, WTF, JavaScriptCore, WebCore: OBJECT libraries; PAL) itself, as WebKit's own
# WPEWebProcess/WPENetworkProcess do there.
set(WPEBrowser_FRAMEWORKS
    bmalloc
    WTF
    JavaScriptCore
    PAL
    WebCore
    WebKit
)

set(WPEBrowser_PRIVATE_LIBRARIES
    PNG::PNG
)

set(WPEBrowser_OUTPUT_NAME wpe-browser)

WEBKIT_EXECUTABLE_DECLARE(WPEBrowser)
WEBKIT_EXECUTABLE(WPEBrowser)

install(TARGETS WPEBrowser DESTINATION "${EXEC_INSTALL_DIR}")
