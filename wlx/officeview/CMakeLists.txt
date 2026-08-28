cmake_minimum_required(VERSION 3.16)
project(officeview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Double Commander plugin settings
set(CMAKE_SHARED_LIBRARY_PREFIX "")
if(UNIX)
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".wlx")
endif()

option(ENABLE_QT6 "Build the Qt6 officeview plugin" ON)
option(ENABLE_GTK3 "Build the GTK3 officeview plugin" ON)

find_package(PkgConfig REQUIRED)
include(ExternalProject)

# Download and build mupdf statically (shared by both UI backends)
set(MUPDF_VERSION "1.25.0")
set(MUPDF_URL "https://github.com/ArtifexSoftware/mupdf-downloads/releases/download/${MUPDF_VERSION}/mupdf-${MUPDF_VERSION}-source.tar.gz")
set(MUPDF_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/mupdf-ext")

ExternalProject_Add(
    mupdf_external
    URL ${MUPDF_URL}
    PREFIX ${MUPDF_PREFIX}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make HAVE_OBJCOPY=no XCFLAGS=-fPIC HAVE_GLUT=no HAVE_X11=no HAVE_GLFW=no build=release libs
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
)

set(MUPDF_SRC_DIR "${MUPDF_PREFIX}/src/mupdf_external")
set(MUPDF_BUILD_DIR "${MUPDF_SRC_DIR}/build/release")

add_library(mupdf STATIC IMPORTED)
set_target_properties(mupdf PROPERTIES IMPORTED_LOCATION "${MUPDF_BUILD_DIR}/libmupdf.a")
add_dependencies(mupdf mupdf_external)

add_library(mupdf-third STATIC IMPORTED)
set_target_properties(mupdf-third PROPERTIES IMPORTED_LOCATION "${MUPDF_BUILD_DIR}/libmupdf-third.a")
add_dependencies(mupdf-third mupdf_external)

# ---------------------------------------------------------------------
# Qt-free core: PdfCore (MuPDF), X2TConverter, LokCore, config, rclone
# ---------------------------------------------------------------------
add_library(officeview_core STATIC
    src/core/OfficeCore.cpp
)
set_target_properties(officeview_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(officeview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MUPDF_SRC_DIR}/include
)
target_link_libraries(officeview_core PUBLIC mupdf mupdf-third freetype harfbuzz)
add_dependencies(officeview_core mupdf_external)

# ---------------------------------------------------------------------
# Qt6 build (LibreOfficeKit-direct + x2t/MuPdfWidget rendering paths)
# ---------------------------------------------------------------------
if(ENABLE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets)
    if(Qt6_FOUND)
        add_library(officeview_qt6 SHARED
            src/officeview.cpp
            src/focus/FocusManager.cpp
            src/mupdfwidget.cpp
        )
        set_target_properties(officeview_qt6 PROPERTIES AUTOMOC ON OUTPUT_NAME "officeview_qt6")
        target_include_directories(officeview_qt6 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${MUPDF_SRC_DIR}/include
        )
        target_link_libraries(officeview_qt6 PRIVATE
            Qt6::Core Qt6::Gui Qt6::Widgets
            mupdf mupdf-third freetype harfbuzz
        )
        add_dependencies(officeview_qt6 mupdf_external)
    else()
        message(WARNING "officeview: Qt6 not found, skipping Qt6 build")
    endif()
endif()

# ---------------------------------------------------------------------
# GTK3 build
# ---------------------------------------------------------------------
if(ENABLE_GTK3)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(GTK3_FOUND)
        add_library(officeview_gtk3 SHARED
            src/gtk3/plugin_gtk3.cpp
        )
        set_target_properties(officeview_gtk3 PROPERTIES OUTPUT_NAME "officeview_gtk3")
        target_include_directories(officeview_gtk3 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
            ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk/include
            ${GTK3_INCLUDE_DIRS}
        )
        target_link_directories(officeview_gtk3 PRIVATE ${GTK3_LIBRARY_DIRS})
        target_compile_options(officeview_gtk3 PRIVATE ${GTK3_CFLAGS_OTHER})
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_gtk)
        target_link_libraries(officeview_gtk3 PRIVATE
            officeview_core
            wlxbase_gtk
            ${GTK3_LIBRARIES}
        )
        # mupdf-third statically bundles its own lcms2/freetype/etc.; without
        # this, those symbols are exported globally from the .wlx and clash
        # at dlopen time with the *system* liblcms2.so.2 already pulled in
        # transitively by GTK (cairo/gdk-pixbuf), which silently interposes
        # mupdf's internal calls into an ABI-incompatible lcms2 and segfaults.
        # -Bsymbolic makes the library prefer its own internal definitions.
        target_link_options(officeview_gtk3 PRIVATE -Wl,-Bsymbolic -Wl,-Bsymbolic-functions)
    else()
        message(WARNING "officeview: GTK3 (gtk+-3.0) not found, skipping GTK3 build")
    endif()
endif()
