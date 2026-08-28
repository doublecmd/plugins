cmake_minimum_required(VERSION 3.16)
project(wlx-log-viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Required for Q_OBJECT macro — generates moc files for vtable/signal-slot metadata
set(CMAKE_AUTOMOC ON)

# Visibility hidden by default as requested in spec
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

option(ENABLE_QT6 "Build Qt6 plugin variant" ON)
option(ENABLE_GTK3 "Build GTK3 plugin variant" ON)

if(ENABLE_QT6)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
endif()
include(FetchContent)

# Must be set before Abseil/RE2 are fetched below: both build as static
# libs that end up linked into a shared .wlx, so they need -fPIC too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# RE2 needs Abseil's CMake targets (absl::absl_check et al.), which are
# newer than what apt ships on e.g. Ubuntu 24.04's libabsl-dev (20220623) --
# fetch a matching-era Abseil ourselves instead of depending on whatever the
# system happens to have, same self-contained-dependency policy used
# elsewhere in this project.
FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG 20240722.0
)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
# RE2's own install(EXPORT re2Targets ...) rule requires every Abseil target
# it depends on to be in an export set too -- ON (not OFF) is what actually
# satisfies that at configure time, even though this project never runs
# `make install` itself.
set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(absl)

FetchContent_Declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2.git
  GIT_TAG 2024-07-02
)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
# Ensure re2 is built as a static library with PIC
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_MakeAvailable(re2)

# Need Qt6::WaylandClient? If not just Widgets and Core
# For inotify, it's native linux.

# De-Qtified core: mmap/RE2/threading logic, zero Qt dependency —
# LogModel is now a thin QAbstractListModel adapter over this (see
# src/LogModel.cpp). Kept as its own static lib (rather than folded into
# the src/core/ subdirectory tree other plugins use) to keep this change
# minimal; a future GTK port would reuse it directly.
add_library(logview_core STATIC
    src/core/LogEngine.h
    src/core/LogEngine.cpp
)
target_include_directories(logview_core PUBLIC src/core)
target_link_libraries(logview_core PUBLIC re2::re2)

# Build the shared library (WLX plugin) — Qt6 variant
if(ENABLE_QT6)
    add_library(logviewer SHARED
        src/wlx_plugin.cpp
        src/LogViewerWidget.cpp
        src/LogModel.cpp
    )

    set_target_properties(logviewer PROPERTIES
        PREFIX ""
        SUFFIX ".wlx"
        OUTPUT_NAME "logviewer"
    )

    target_link_libraries(logviewer PRIVATE
        logview_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        re2::re2
    )
endif()

# Only export specific functions
# This is usually handled by `extern "C" __attribute__((visibility("default")))` in the code,
# combined with the hidden preset.

# --- GTK3 Variant ---
# LogTreeModel is a custom virtual GtkTreeModel over LogEngine directly
# (not a GtkListStore) — preserves the mmap-based scalability that's the
# entire point of LogEngine for huge log files.
if(ENABLE_GTK3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(GTK3_FOUND)
        add_library(logviewer_gtk3 SHARED
            src/gtk3/LogTreeModel.h
            src/gtk3/LogTreeModel.cpp
            src/gtk3/LogViewerWidget_gtk3.cpp
        )
        target_include_directories(logviewer_gtk3 PRIVATE
            ${GTK3_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        )
        target_link_libraries(logviewer_gtk3 PRIVATE logview_core ${GTK3_LIBRARIES})
        # Deliberately NOT -static-libstdc++/-static-libgcc. Confirmed via a
        # live gdb session (crash inside saveHighlightRules' `ofstream <<
        # int` call, faulting in codecvt<char16_t,...>::do_unshift) that
        # statically linking libstdc++ gives this .wlx its own private copy
        # of C++ locale/iostream facet templates, separate from the single
        # libstdc++.so.6 every other component in DC's process shares. Two
        # independent copies registering facet IDs against the same
        # process-wide registry causes a facet lookup to resolve to the
        # wrong vtable -- the same root cause already fixed for
        # diagramview_gtk3 and markdownview_gtk3 (see their CMakeLists.txt
        # for the full writeup). structview, which never statically links
        # libstdc++, never showed this symptom. Sharing the dynamic
        # libstdc++.so (the default; no special flag needed) removes the
        # mechanism outright.
        set_target_properties(logviewer_gtk3 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "logviewer_gtk3"
            SUFFIX ".wlx"
        )
    else()
        message(STATUS "logview: gtk+-3.0 not found — GTK3 variant skipped")
    endif()
endif()
