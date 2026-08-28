cmake_minimum_required(VERSION 3.16)
project(mpv_wayland LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_QT6 "Build Qt6 plugin variant" ON)
option(ENABLE_GTK3 "Build GTK3 plugin variant" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv)

# --- Shared core: libmpv client + render-GL wrapper, zero Qt/GTK dependency ---
add_library(mpv_wayland_core STATIC
    src/core/MpvEngine.h
    src/core/MpvEngine.cpp
)
target_include_directories(mpv_wayland_core PUBLIC src/core ${MPV_INCLUDE_DIRS})
target_link_libraries(mpv_wayland_core PUBLIC ${MPV_LIBRARIES})

# --- Qt6 Variant ---
if(ENABLE_QT6)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
    set(CMAKE_AUTOMOC ON)

    add_library(mpv_wayland_qt6 SHARED
        src/qt6/main_qt6.cpp
        src/qt6/mpvwidget.cpp
    )
    target_include_directories(mpv_wayland_qt6 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        src/qt6
    )
    target_link_libraries(mpv_wayland_qt6 PRIVATE
        mpv_wayland_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGLWidgets
    )
    set_target_properties(mpv_wayland_qt6 PROPERTIES
        PREFIX ""
        OUTPUT_NAME "mpv_wayland_qt6"
        SUFFIX ".wlx"
        AUTOMOC ON
    )
endif()

# --- GTK3 Variant ---
if(ENABLE_GTK3)
    pkg_check_modules(GTK3 gtk+-3.0 epoxy)
    if(GTK3_FOUND)
        add_library(mpv_wayland_gtk3 MODULE
            src/gtk3/plugin_gtk3.cpp
        )
        target_include_directories(mpv_wayland_gtk3 PRIVATE
            ${GTK3_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        )
        target_link_libraries(mpv_wayland_gtk3 PRIVATE mpv_wayland_core ${GTK3_LIBRARIES})
        target_link_options(mpv_wayland_gtk3 PRIVATE -static-libstdc++ -static-libgcc)
        set_target_properties(mpv_wayland_gtk3 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "mpv_wayland_gtk3"
            SUFFIX ".wlx"
        )
    else()
        message(STATUS "mpv_wayland: gtk+-3.0/epoxy not found — GTK3 variant skipped")
    endif()
endif()
