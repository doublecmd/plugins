cmake_minimum_required(VERSION 3.16)
project(certview CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_QT6 "Build Qt6 plugin variant" ON)
option(ENABLE_GTK3 "Build GTK3 plugin variant" ON)

# --- Vendored MbedTLS (built as static PIC library with all crypto/x509 files) ---
set(MBEDTLS_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mbedtls)

file(GLOB MBEDTLS_SOURCES
    ${MBEDTLS_SRC_DIR}/library/*.c
)
# Exclude network/SSL sockets that aren't needed for certificate viewing
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX "net_sockets\\.c$")

add_library(vendored_mbedtls STATIC ${MBEDTLS_SOURCES})
set_target_properties(vendored_mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vendored_mbedtls PUBLIC
    ${MBEDTLS_SRC_DIR}/include
    ${MBEDTLS_SRC_DIR}/library
)

# --- Shared Core Library (UI-agnostic) ---
add_library(certview_core STATIC
    src/core/CertModel.h
    src/core/CertParser.h
    src/core/CertParser.cpp
)
set_target_properties(certview_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(certview_core PUBLIC
    src/core
    ${MBEDTLS_SRC_DIR}/include
)
target_link_libraries(certview_core PUBLIC
    vendored_mbedtls
)

# --- Qt6 Plugin Variant ---
if(ENABLE_QT6)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
    set(CMAKE_AUTOMOC ON)

    # Base support library
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_wlqt
        ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_wlqt
    )

    add_library(certview_qt6 SHARED
        src/qt6/CertViewWidget.h
        src/qt6/CertViewWidget.cpp
        src/qt6/plugin_qt6.cpp
    )

    set_target_properties(certview_qt6 PROPERTIES
        PREFIX ""
        SUFFIX ".wlx"
        AUTOMOC ON
    )

    target_include_directories(certview_qt6 PRIVATE
        src
        src/core
        src/qt6
        ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    )

    target_link_libraries(certview_qt6 PRIVATE
        certview_core
        wlxbase_wlqt
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
    )
endif()

# --- GTK3 Plugin Variant ---
if(ENABLE_GTK3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(GTK3_FOUND)
        add_library(certview_gtk3 MODULE
            src/gtk3/plugin_gtk3.cpp
        )
        set_target_properties(certview_gtk3 PROPERTIES AUTOMOC OFF)
        target_include_directories(certview_gtk3 PRIVATE
            src
            src/core
            ${GTK3_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        )
        target_link_libraries(certview_gtk3 PRIVATE
            certview_core
            ${GTK3_LIBRARIES}
        )
        target_link_options(certview_gtk3 PRIVATE -static-libstdc++ -static-libgcc)
        set_target_properties(certview_gtk3 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "certview_gtk3"
            SUFFIX ".wlx"
        )
    else()
        message(STATUS "certview: gtk+-3.0 not found — GTK3 variant skipped")
    endif()
endif()
