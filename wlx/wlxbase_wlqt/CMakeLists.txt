cmake_minimum_required(VERSION 3.16)
project(wlxbase_wlqt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)

# ---------------------------------------------------------------------------
# enca — downloaded and built as a static library
# ---------------------------------------------------------------------------
set(ENCA_VERSION "1.19")
set(ENCA_DIR "${CMAKE_CURRENT_BINARY_DIR}/enca-${ENCA_VERSION}")
set(ENCA_TAR "${CMAKE_CURRENT_BINARY_DIR}/enca-${ENCA_VERSION}.tar.gz")
set(ENCA_LIB "${ENCA_DIR}/lib/.libs/libenca.a")

if(NOT EXISTS "${ENCA_LIB}")
    if(NOT EXISTS "${ENCA_TAR}")
        message(STATUS "Downloading enca ${ENCA_VERSION}...")
        file(DOWNLOAD
            "https://dl.cihar.com/enca/enca-${ENCA_VERSION}.tar.gz"
            "${ENCA_TAR}"
            SHOW_PROGRESS
        )
    endif()
    message(STATUS "Extracting and building enca...")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${ENCA_TAR}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    execute_process(COMMAND ./configure --disable-shared --enable-static --with-pic
                    WORKING_DIRECTORY "${ENCA_DIR}")
    execute_process(COMMAND make -j4
                    WORKING_DIRECTORY "${ENCA_DIR}")
endif()

add_library(enca_static STATIC IMPORTED)
set_target_properties(enca_static PROPERTIES
    IMPORTED_LOCATION "${ENCA_LIB}"
)

# ---------------------------------------------------------------------------
# wlxbase_wlqt static library
# ---------------------------------------------------------------------------
file(GLOB BASE_HEADERS "include/wlxbase_wlqt/*.h")

add_library(wlxbase_wlqt STATIC
    ${BASE_HEADERS}
    src/FocusManager.cpp
    src/PluginToolBar.cpp
    src/EditableGridWidget.cpp
    src/FindReplacePanel.cpp
    src/ScopedFindReplacePanel.cpp
    src/EncodingUtils.cpp
    src/FilterRowWidget.cpp
    src/FilterableHeaderView.cpp
    src/PluginStatusBar.cpp
    src/PluginSplitView.cpp
    src/ThemeManager.cpp
    src/CrashLogger.cpp
    src/SequentialRowProxyModel.cpp
)

target_include_directories(wlxbase_wlqt
    PUBLIC
        include
    PRIVATE
        "${ENCA_DIR}/lib"
        ${GLIB2_INCLUDE_DIRS}
)

target_link_libraries(wlxbase_wlqt
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    PRIVATE
        enca_static
        ${GLIB2_LIBRARIES}
        ${CMAKE_DL_LIBS}
)
