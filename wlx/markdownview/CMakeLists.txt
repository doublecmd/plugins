cmake_minimum_required(VERSION 3.16)
project(markdownview VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_QT6 "Build the Qt6 markdownview plugin" ON)
option(ENABLE_GTK3 "Build the GTK3 markdownview plugin" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(RSVG REQUIRED librsvg-2.0)

include(FetchContent)

# md4c: a plain-C, toolkit-agnostic CommonMark/GFM parser (replaces md4qt,
# which forced Qt6::Core on both plugin targets via its QString-typed AST
# and turned out to need Qt 6.5+ APIs this project doesn't otherwise
# require). md4c-html is its official HTML-renderer companion, used as-is
# for everything except fenced ```mermaid/plantuml code blocks and
# $...$/$$...$$ LaTeX math spans (rendered via md_html()'s
# MD_FLAG_LATEXMATHSPANS as <x-equation> tags) -- both are post-processed
# out of the resulting HTML in markdown_engine.cpp. See
# https://github.com/mity/md4c.
FetchContent_Declare(md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        release-0.5.2
    GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c)

set(tinyxml2_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/tinyxml2)

# MICROTEX_NO_AUTO_BACKEND: build the LaTeX static library with no
# Graphics2D backend baked in at all -- each plugin target links its own
# backend implementation directly (src/qt6/latex_render_qt.cpp using
# MicroTeX's Qt backend, src/gtk3/latex_render_cairo.cpp using its Cairo
# backend) instead of forcing one choice onto the shared library. See the
# comment on that option in 3rdparty/MicroTeX/CMakeLists.txt.
# MicroTeX defaults both of these ON, which dumps its entire font/symbol
# tables (thousands of lines) to stdout on every single render -- fine for
# its own sample apps, not for a plugin embedded in a file manager.
set(GRAPHICS_DEBUG OFF CACHE BOOL "" FORCE)
set(HAVE_LOG OFF CACHE BOOL "" FORCE)
set(MICROTEX_NO_AUTO_BACKEND ON CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/MicroTeX)

# Core Markdown Rendering Library (Static Library) -- toolkit-neutral:
# md4c (parsing), librsvg+Cairo (SVG rasterization for Mermaid/PlantUML),
# a curl subprocess (fetching those web renders), MicroTeX's core (LaTeX
# layout, backend-agnostic) -- no Qt, no GTK. Each plugin target supplies
# its own renderLatexToPng() implementation (see src/qt6/, src/gtk3/) and
# links it alongside this.
add_library(markdownview_core STATIC
    src/core/markdown_engine.cpp
    src/core/diagram_render.cpp
)

target_include_directories(markdownview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/MicroTeX/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    ${CAIRO_INCLUDE_DIRS}
    ${RSVG_INCLUDE_DIRS}
    # Explicit rather than relying on md4c-html's PUBLIC usage requirement
    # propagating through target_link_libraries -- observed to not always
    # take effect (environment/CMake-version sensitive), so pinned directly.
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(markdownview_core PUBLIC
    md4c-html
    LaTeX
    ${CAIRO_LIBRARIES}
    ${RSVG_LIBRARIES}
)
target_link_directories(markdownview_core PUBLIC ${CAIRO_LIBRARY_DIRS} ${RSVG_LIBRARY_DIRS})
target_compile_options(markdownview_core PUBLIC ${CAIRO_CFLAGS_OTHER} ${RSVG_CFLAGS_OTHER})

# ---------------------------------------------------------------------
# Qt6 Lister Plugin Target
# ---------------------------------------------------------------------
if(ENABLE_QT6)
    find_package(Qt6 6.2 COMPONENTS Core Gui Widgets PrintSupport)
    if(Qt6_FOUND)
        add_library(markdownview_qt6 MODULE
            src/qt6/plugin_qt6.cpp
            src/qt6/latex_render_qt.cpp
            3rdparty/MicroTeX/src/platform/qt/graphic_qt.cpp
        )
        set_target_properties(markdownview_qt6 PROPERTIES AUTOMOC ON)
        # graphic_qt.h's tex::Graphics2D_qt is compiled out unless BUILD_QT
        # is defined -- normally set PUBLIC on the LaTeX target itself by
        # its own CMakeLists' `if(QT)` branch, but MICROTEX_NO_AUTO_BACKEND
        # skips that (see the option's comment above), so this target
        # defines it directly instead, same as markdownview_gtk3 does with
        # BUILD_GTK for graphic_cairo.h.
        target_compile_definitions(markdownview_qt6 PRIVATE BUILD_QT)

        target_link_libraries(markdownview_qt6 PRIVATE
            markdownview_core
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::PrintSupport
        )

        # Statically link standard C++ runtime to prevent GLIBCXX version
        # mismatches across environments.
        target_link_options(markdownview_qt6 PRIVATE
            -static-libstdc++
            -static-libgcc
        )

        set_target_properties(markdownview_qt6 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "markdownview_qt6"
            SUFFIX ".wlx"
        )
    else()
        message(WARNING "markdownview: Qt6 not found, skipping Qt6 build")
    endif()
endif()

# ---------------------------------------------------------------------
# GTK3 Lister Plugin Target
# ---------------------------------------------------------------------
if(ENABLE_GTK3)
    pkg_check_modules(GTK3 gtk+-3.0 webkit2gtk-4.1)
    pkg_check_modules(CairoMM cairomm-1.0)
    pkg_check_modules(PangoMM pangomm-1.4)
    pkg_check_modules(Freetype2 freetype2)
    find_package(Fontconfig)
    if(GTK3_FOUND AND CairoMM_FOUND AND PangoMM_FOUND AND Fontconfig_FOUND AND Freetype2_FOUND)
        add_library(markdownview_gtk3 MODULE
            src/gtk3/plugin_gtk3.cpp
            src/gtk3/latex_render_cairo.cpp
            3rdparty/MicroTeX/src/platform/cairo/graphic_cairo.cpp
        )
        target_compile_definitions(markdownview_gtk3 PRIVATE BUILD_GTK_TARGET BUILD_GTK)
        target_include_directories(markdownview_gtk3 PRIVATE
            ${GTK3_INCLUDE_DIRS}
            ${CairoMM_INCLUDE_DIRS}
            ${PangoMM_INCLUDE_DIRS}
            ${Freetype2_INCLUDE_DIRS}
        )
        target_link_directories(markdownview_gtk3 PRIVATE
            ${GTK3_LIBRARY_DIRS} ${CairoMM_LIBRARY_DIRS} ${PangoMM_LIBRARY_DIRS} ${Freetype2_LIBRARY_DIRS}
        )
        target_compile_options(markdownview_gtk3 PRIVATE
            ${GTK3_CFLAGS_OTHER} ${CairoMM_CFLAGS_OTHER} ${PangoMM_CFLAGS_OTHER} ${Freetype2_CFLAGS_OTHER}
        )
        target_link_libraries(markdownview_gtk3 PRIVATE
            markdownview_core
            ${GTK3_LIBRARIES}
            ${CairoMM_LIBRARIES}
            ${PangoMM_LIBRARIES}
            ${Freetype2_LIBRARIES}
            Fontconfig::Fontconfig
        )
        # Deliberately NOT -static-libstdc++/-static-libgcc (unlike the qt6
        # target above). Confirmed via a live gdb session: statically linking
        # libstdc++ gives this .wlx its own private copy of C++ locale/
        # iostream facet templates, separate from the single libstdc++.so
        # every other component in DC's process shares. When two independent
        # copies of that machinery register facet IDs against the same
        # process-wide registry, a subsequent facet lookup can resolve to the
        # WRONG facet's vtable -- reproduced as a char-based
        # regex_traits<char> call landing inside an unrelated
        # codecvt<char16_t,...>::do_unshift, crashing on DC's own main
        # thread. structview, which never statically links libstdc++, never
        # showed this symptom. Sharing the dynamic libstdc++.so (the default;
        # no special flag needed) removes the mechanism outright.
        set_target_properties(markdownview_gtk3 PROPERTIES
            PREFIX ""
            OUTPUT_NAME "markdownview_gtk3"
            SUFFIX ".wlx"
        )
    else()
        message(WARNING "markdownview: GTK3/cairomm-1.0/pangomm-1.4 not found, skipping GTK3 build")
    endif()
endif()
