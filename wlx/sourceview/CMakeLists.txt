cmake_minimum_required(VERSION 3.20)
project(sourceview_wlx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
# Not otherwise used directly (libxml2 is vendored+built from source
# below instead, see that section for why) -- kept as a fail-fast sanity
# check, since the vendored gtksourceview4 build's own meson configure
# step looks up libxml-2.0 via the system pkg-config too, for its own
# compile-time headers.
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)

# --- GtkSourceView 4, vendored + built statically ---
#
# This is the GTK analog of kate_qt6's use of KDE's KTextEditor -- a real,
# richly-featured syntax-highlighting editor widget, not a from-scratch
# reimplementation. Built from source (Meson, GtkSourceView's only build
# system) rather than linked against the system's libgtksourceview-4-0
# for two reasons:
#   1. The user wants a drop-in .wlx with zero install-time dependencies
#      beyond what a GTK3 host (i.e. Double Commander itself) already
#      guarantees -- linking the system .so would require every user to
#      separately apt-install libgtksourceview-4-0 first.
#   2. GtkSourceView's bundled syntax-highlighting language specs and
#      color style schemes (data/language-specs/*.lang,
#      data/styles/*.xml) are compiled into the library's own GResource
#      at build time (confirmed via GNOME's own docs/build files) -- so a
#      from-source static build bakes the highlighting *data* into this
#      plugin binary too, not just the code. Linking the system .so would
#      still work at the API level but relies on the system also having
#      /usr/share/gtksourceview-4/ populated, which isn't guaranteed on a
#      machine that only has this .wlx and no GtkSourceView-using apps
#      installed.
#
# GTK3/glib/gobject/gio/fribidi are NOT vendored here -- Double Commander
# itself is a GTK3 process, so these are already guaranteed loaded into
# any host this plugin runs inside (same assumption every other GTK3
# plugin in this project makes). libxml2 is GtkSourceView's one real
# *extra* dependency beyond baseline GTK3, and is vendored+built from
# source too: the apt-shipped libxml2.a exists but was built without
# -fPIC (confirmed via a real link failure -- "relocation ... can not be
# used when making a shared object"), so it can't go into this shared
# .wlx as-is. Its own runtime deps (ICU/zlib/liblzma/libm) are NOT
# vendored -- same reasoning as GTK3 itself, these are base-system
# libraries on any mainstream Linux desktop, unlike libxml2's *static
# archive specifically*, which distros don't build PIC by default.
include(ExternalProject)

set(XML2_VERSION "v2.13.9")
set(XML2_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/libxml2")
set(XML2_INSTALL "${XML2_PREFIX}/install")

ExternalProject_Add(libxml2_ext
    GIT_REPOSITORY https://gitlab.gnome.org/GNOME/libxml2.git
    GIT_TAG        ${XML2_VERSION}
    GIT_SHALLOW    TRUE
    PREFIX         ${XML2_PREFIX}
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${XML2_INSTALL}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF
        -DLIBXML2_WITH_PYTHON=OFF
        -DLIBXML2_WITH_TESTS=OFF
        -DLIBXML2_WITH_ICONV=OFF
        -DLIBXML2_WITH_LZMA=OFF
        -DLIBXML2_WITH_ZLIB=OFF
    BUILD_BYPRODUCTS ${XML2_INSTALL}/lib/libxml2.a
)
file(MAKE_DIRECTORY "${XML2_INSTALL}/include/libxml2")

add_library(libxml2_static STATIC IMPORTED GLOBAL)
set_target_properties(libxml2_static PROPERTIES
    IMPORTED_LOCATION "${XML2_INSTALL}/lib/libxml2.a"
)
target_include_directories(libxml2_static INTERFACE "${XML2_INSTALL}/include/libxml2")
add_dependencies(libxml2_static libxml2_ext)

set(GSV_VERSION "4.8.4")
set(GSV_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gtksourceview")
set(GSV_INSTALL "${GSV_PREFIX}/install")

# GtkSourceView's own meson.build always builds its actual public target
# (gtksource_lib) as a shared_library() -- --default-library=static is
# silently ignored for it (only its two *internal* helper libraries,
# core_lib and completion-providers/words' completionwords_lib, are real
# static_library() targets, and even those are built with `install:
# false`, i.e. meson builds them but won't install them anywhere stable).
# Patched via sed to `install: true` so a normal `ninja install` gives us
# stable paths for both -- these two together are the whole of
# GtkSourceView's actual code (the shared lib is just `link_whole:` of
# both of them), so linking both with --whole-archive (below) is
# equivalent to linking the "real" library.
ExternalProject_Add(gtksourceview4_ext
    GIT_REPOSITORY https://gitlab.gnome.org/GNOME/gtksourceview.git
    GIT_TAG        ${GSV_VERSION}
    GIT_SHALLOW    TRUE
    PREFIX         ${GSV_PREFIX}
    PATCH_COMMAND
        sed -i "s/install: false$/install: true/" <SOURCE_DIR>/gtksourceview/meson.build &&
        sed -i "s/install: false,/install: true,/" <SOURCE_DIR>/gtksourceview/completion-providers/words/meson.build
    CONFIGURE_COMMAND meson setup <BINARY_DIR> <SOURCE_DIR>
        --prefix=${GSV_INSTALL}
        --libdir=lib
        --default-library=static
        --buildtype=release
        -Dgtk_doc=false
        -Dvapi=false
        -Dgir=false
        -Dinstall_tests=false
        -Dglade_catalog=false
    BUILD_COMMAND     ninja -C <BINARY_DIR>
    INSTALL_COMMAND   ninja -C <BINARY_DIR> install
    BUILD_BYPRODUCTS  ${GSV_INSTALL}/lib/libgtksourceview-4core.a
                      ${GSV_INSTALL}/lib/libgtksourceview-4completionwords.a
)

# CMake validates INTERFACE_INCLUDE_DIRECTORIES on IMPORTED targets exist
# at *configure* time, but this directory is only populated later by the
# ExternalProject's own build/install step -- pre-create it as an empty
# placeholder so configure succeeds; add_dependencies() below still
# guarantees the real headers land there before anything compiles against
# it (same pattern as dbview's vendored Berkeley DB).
file(MAKE_DIRECTORY "${GSV_INSTALL}/include/gtksourceview-4")

add_library(gtksourceview4 INTERFACE IMPORTED GLOBAL)
target_include_directories(gtksourceview4 INTERFACE "${GSV_INSTALL}/include/gtksourceview-4")
# --whole-archive: GObject classes (e.g. GtkSourceLanguage, the various
# gutter renderers) are registered via GType machinery, not referenced by
# an ordinary C symbol from consuming code -- a normal partial static
# link would drop their object files as "unused" since nothing in our own
# code calls into them directly by name.
target_link_options(gtksourceview4 INTERFACE
    -Wl,--whole-archive
    "${GSV_INSTALL}/lib/libgtksourceview-4core.a"
    "${GSV_INSTALL}/lib/libgtksourceview-4completionwords.a"
    -Wl,--no-whole-archive
)
add_dependencies(gtksourceview4 gtksourceview4_ext)

# ---------------------------------------------------------------------------
# Plugin library
# ---------------------------------------------------------------------------
# GtkFocusManager / GtkPluginToolBar / GtkFindReplacePanel -- the same
# shared GTK3 UI-chrome helpers csvview/dbview/structview build on.
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk
    ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_gtk
)

add_library(sourceview_gtk3 SHARED
    src/plugin_gtk3.cpp
    src/EditorWidget.cpp
)

target_include_directories(sourceview_gtk3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk/include
    ${GTK3_INCLUDE_DIRS}
)
target_link_directories(sourceview_gtk3 PRIVATE ${GTK3_LIBRARY_DIRS})
target_compile_options(sourceview_gtk3 PRIVATE ${GTK3_CFLAGS_OTHER})

target_link_libraries(sourceview_gtk3 PRIVATE
    wlxbase_gtk
    gtksourceview4
    ${GTK3_LIBRARIES}
    libxml2_static
    m
)

# Same rationale as officeview/dbview: statically-linked third-party code
# (gtksourceview4, libxml2 here) gets exported from the .wlx by default
# and can be interposed by whatever the host process already loaded of a
# same-named symbol (e.g. DC or another plugin's own libxml2, if any).
target_link_options(sourceview_gtk3 PRIVATE -Wl,-Bsymbolic -Wl,-Bsymbolic-functions -static-libstdc++ -static-libgcc)

set_target_properties(sourceview_gtk3 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sourceview_gtk3"
    SUFFIX ".wlx"
)
