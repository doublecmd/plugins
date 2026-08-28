cmake_minimum_required(VERSION 3.16)
project(wlxbase_gtk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

add_library(wlxbase_gtk STATIC
    include/wlxbase_gtk/GtkFocusManager.h
    include/wlxbase_gtk/GtkPluginToolBar.h
    include/wlxbase_gtk/GtkEditableGridWidget.h
    include/wlxbase_gtk/GtkFindReplacePanel.h
    include/wlxbase_gtk/GtkScopedFindReplacePanel.h
    src/GtkFocusManager.cpp
    src/GtkPluginToolBar.cpp
    src/GtkEditableGridWidget.cpp
    src/GtkFindReplacePanel.cpp
    src/GtkScopedFindReplacePanel.cpp
)

target_include_directories(wlxbase_gtk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GTK3_INCLUDE_DIRS}
)
target_link_libraries(wlxbase_gtk PUBLIC ${GTK3_LIBRARIES})
target_compile_options(wlxbase_gtk PUBLIC ${GTK3_CFLAGS_OTHER})
