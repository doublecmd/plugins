cmake_minimum_required(VERSION 3.16)
project(kate_qt6 VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6TextEditor REQUIRED)
find_package(KF6Config REQUIRED)

add_library(kate_qt6 SHARED
    src/wlx_plugin.cpp
    src/editor_widget.cpp
)

target_compile_definitions(kate_qt6 PRIVATE
    RICH_EDITOR_QT_LOG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/logs"
)

target_link_libraries(kate_qt6 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::TextEditor
    KF6::ConfigCore
)

set_target_properties(kate_qt6 PROPERTIES
    PREFIX ""
    SUFFIX ".wlx"
)
