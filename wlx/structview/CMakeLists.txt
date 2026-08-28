cmake_minimum_required(VERSION 3.20)
project(structview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
if(UNIX)
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".wlx")
endif()

option(ENABLE_QT6 "Build the Qt6 structview plugin" ON)
option(ENABLE_GTK3 "Build the GTK3 structview plugin" ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

find_package(yaml-cpp REQUIRED)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        9.0.0
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(tomlplusplus nlohmann_json tinyxml2)

# ---------------------------------------------------------------------
# Qt-free core: DocumentNode/TextFormatEngine + all 6 format engines
# ---------------------------------------------------------------------
add_library(structview_core STATIC
    src/core/JsonEngine.cpp
    src/core/CborEngine.cpp
    src/core/XmlEngine.cpp
    src/core/IniEngine.cpp
    src/core/YamlEngine.cpp
    src/core/TomlEngine.cpp
)
target_include_directories(structview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(structview_core PUBLIC
    yaml-cpp
    tomlplusplus::tomlplusplus
    nlohmann_json::nlohmann_json
    tinyxml2::tinyxml2
)

# ---------------------------------------------------------------------
# Qt6 build (unchanged legacy Qt-only engines/widget)
# ---------------------------------------------------------------------
if(ENABLE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets Xml PrintSupport)
    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_subdirectory(
            ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_wlqt
            ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_wlqt
        )
        add_library(structview_qt6 SHARED
            include/StructViewWidget.h
            include/TextFormatEngine.h
            src/wlx_entry.cpp
            src/StructViewWidget.cpp
            src/JsonEngine.cpp
            src/XmlEngine.cpp
            src/IniEngine.cpp
            src/CborEngine.cpp
            src/YamlEngine.cpp
            src/TomlEngine.cpp
        )
        set_target_properties(structview_qt6 PROPERTIES PREFIX "" SUFFIX ".wlx")
        target_include_directories(structview_qt6 PRIVATE
            include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
        )
        target_link_libraries(structview_qt6 PRIVATE
            wlxbase_wlqt
            Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Xml Qt6::PrintSupport
            yaml-cpp
            tomlplusplus::tomlplusplus
        )
    else()
        message(WARNING "structview: Qt6 not found, skipping Qt6 build")
    endif()
endif()

# ---------------------------------------------------------------------
# GTK3 build
# ---------------------------------------------------------------------
if(ENABLE_GTK3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(GTK3_FOUND)
        add_library(structview_gtk3 SHARED
            src/gtk3/plugin_gtk3.cpp
        )
        set_target_properties(structview_gtk3 PROPERTIES PREFIX "" SUFFIX ".wlx")
        target_include_directories(structview_gtk3 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
            ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk/include
            ${GTK3_INCLUDE_DIRS}
        )
        target_link_directories(structview_gtk3 PRIVATE ${GTK3_LIBRARY_DIRS})
        target_compile_options(structview_gtk3 PRIVATE ${GTK3_CFLAGS_OTHER})
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_gtk)
        target_link_libraries(structview_gtk3 PRIVATE
            structview_core
            wlxbase_gtk
            ${GTK3_LIBRARIES}
        )
    else()
        message(WARNING "structview: GTK3 (gtk+-3.0) not found, skipping GTK3 build")
    endif()
endif()
