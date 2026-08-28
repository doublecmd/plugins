cmake_minimum_required(VERSION 3.20)
project(diagramview_wlx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_QT6 "Build Qt6 plugin variant" ON)
option(ENABLE_GTK3 "Build GTK3 plugin variant" ON)

# --- Shared core: CLI subprocess runner + settings, zero Qt/GTK dependency ---
add_library(diagramview_core STATIC
    src/core/DiagramRenderer.h
    src/core/DiagramRenderer.cpp
)
target_include_directories(diagramview_core PUBLIC src/core)

# --- Qt6 Variant ---
if(ENABLE_QT6)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Svg SvgWidgets)
    set(CMAKE_AUTOMOC ON)

    add_library(diagramview_qt6 MODULE
        src/qt6/plugin_qt6.cpp
    )
    target_include_directories(diagramview_qt6 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    )
    target_compile_definitions(diagramview_qt6 PRIVATE PLUGNAME="diagramview")
    target_link_libraries(diagramview_qt6 PRIVATE
        diagramview_core
        Qt6::Widgets
        Qt6::Svg
        Qt6::SvgWidgets
    )
    target_link_options(diagramview_qt6 PRIVATE -static-libstdc++ -static-libgcc)
    set_target_properties(diagramview_qt6 PROPERTIES
        PREFIX ""
        OUTPUT_NAME "diagramview_qt6"
        SUFFIX ".wlx"
        AUTOMOC ON
    )
endif()

# --- GTK3 Variant ---
if(ENABLE_GTK3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0 librsvg-2.0 gio-2.0)
    if(GTK3_FOUND)
        add_library(diagramview_gtk3 MODULE
            src/gtk3/plugin_gtk3.cpp
        )
        target_include_directories(diagramview_gtk3 PRIVATE
            ${GTK3_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        )
        target_compile_definitions(diagramview_gtk3 PRIVATE PLUGNAME="diagramview")
        target_link_libraries(diagramview_gtk3 PRIVATE diagramview_core ${GTK3_LIBRARIES})
        # Deliberately NOT -static-libstdc++/-static-libgcc (unlike the qt6
        # target above). Confirmed via a live gdb session: statically linking
        # libstdc++ gives this .wlx its own private copy of C++ locale/
        # iostream facet templates, separate from the single libstdc++.so
        # every other component in DC's process (including its own runtime
        # and other dynamically-linked plugins) shares. When two independent
        # copies of that machinery register facet IDs against the same
        # process-wide registry, a subsequent facet lookup can resolve to the
        # WRONG facet's vtable -- reproduced as a char-based
        # regex_traits<char>/ostream numeric-insertion call landing inside an
        # unrelated codecvt<char16_t,...>::do_unshift, crashing on DC's own
        # main thread. structview, which never statically links libstdc++,
        # never showed this symptom. Sharing the dynamic libstdc++.so (the
        # default; no special flag needed) removes the mechanism outright.
        set_target_properties(diagramview_gtk3 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "diagramview_gtk3"
            SUFFIX ".wlx"
        )
    else()
        message(STATUS "diagramview: gtk+-3.0/librsvg-2.0/gio-2.0 not found — GTK3 variant skipped")
    endif()
endif()
