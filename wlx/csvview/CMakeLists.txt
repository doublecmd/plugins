cmake_minimum_required(VERSION 3.16)
project(csvview_qt6 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_QT6 "Build Qt6 plugin variant" ON)
option(ENABLE_GTK3 "Build GTK3 plugin variant" ON)

# --- Shared core: CSV/TSV tokenizer + serializer, zero Qt/GTK dependency ---
add_library(csvview_core STATIC
    src/core/CsvCore.h
    src/core/CsvCore.cpp
)
target_include_directories(csvview_core PUBLIC src/core)

# --- Qt6 Variant ---
if(ENABLE_QT6)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)
    set(CMAKE_AUTOMOC ON)

    # Pull in the base library (builds libwlxbase_wlqt.a)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_wlqt
        ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_wlqt
    )

    add_library(csvview_qt6 SHARED
        src/plugin.cpp
    )

    set_target_properties(csvview_qt6 PROPERTIES
        PREFIX ""
        SUFFIX ".wlx"
        AUTOMOC ON
    )

    target_include_directories(csvview_qt6 PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    )

    target_link_libraries(csvview_qt6 PRIVATE
        csvview_core
        wlxbase_wlqt
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        Qt6::PrintSupport
    )
endif()

# --- GTK3 Variant ---
# Full parity build on wlxbase_gtk (GtkFocusManager, GtkEditableGridWidget
# with undo/redo, GtkScopedFindReplacePanel) — the GTK counterpart to
# wlxbase_wlqt used by the Qt6 build above.
if(ENABLE_GTK3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(GTK3_FOUND)
        add_subdirectory(
            ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk
            ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_gtk
        )

        add_library(csvview_gtk3 MODULE
            src/gtk3/plugin_gtk3.cpp
        )
        target_include_directories(csvview_gtk3 PRIVATE
            ${GTK3_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        )
        target_link_libraries(csvview_gtk3 PRIVATE csvview_core wlxbase_gtk ${GTK3_LIBRARIES})
        target_link_options(csvview_gtk3 PRIVATE -static-libstdc++ -static-libgcc)
        set_target_properties(csvview_gtk3 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "csvview_gtk3"
            SUFFIX ".wlx"
        )
    else()
        message(STATUS "csvview: gtk+-3.0 not found — GTK3 variant skipped")
    endif()
endif()
