# SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.19)

project(md4qt VERSION "5.1.3")

set(MD4QT_VERSION ${PROJECT_VERSION})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BUILD_MD4QT_BENCHMARK "Build benchmark? Default OFF." OFF)
option(BUILD_MD4QT_TESTS "Build tests with Qt support? Default ON." ON)
option(BUILD_MD2HTML_APP "Build md2html utility? Default OFF." OFF)
option(BUILD_MD2QDOC_APP "Build md2qdoc utility? Default OFF." OFF)
option(INSTALL_MD4QT "Install md4qt targets? Default ON." ON)

set(QT_MIN_VERSION "6.0")
set(KF_MIN_VERSION "6.0")

find_package(ECM ${KF_MIN_VERSION} NO_MODULE)

if(ECM_FOUND)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_BINARY_DIR})

if(ECM_FOUND)
    include(KDEInstallDirs)
    include(KDECMakeSettings)
    include(KDECompilerSettings NO_POLICY_SCOPE)
    include(ECMSetupVersion)

    if(ECM_VERSION VERSION_GREATER_EQUAL "6.11.0")
        include(ECMGenerateQDoc)
    endif()

    include(KDEGitCommitHooks)
else()
    set(CMAKE_AUTOMOC ON)
endif()

set(CMAKE_WIN32_EXECUTABLE FALSE)
set(CMAKE_MACOSX_BUNDLE FALSE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release"
         CACHE STRING "Choose the type of build."
         FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

if(BUILD_MD4QT_TESTS)
    enable_testing()

    add_subdirectory(tests)
endif()

if(BUILD_MD4QT_BENCHMARK)
    add_subdirectory(tests/md_benchmark)
endif(BUILD_MD4QT_BENCHMARK)

file(GLOB_RECURSE HDR src/*.h)
file(GLOB_RECURSE SRC src/*.cpp)

if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif(ENABLE_COVERAGE)

add_library(md4qt STATIC ${HDR} ${SRC})
add_library(md4qt::md4qt ALIAS md4qt)

find_package(Qt6 REQUIRED COMPONENTS Core)

set_target_properties(md4qt PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(md4qt PRIVATE Qt6::Core)

target_include_directories(md4qt INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(ECM_FOUND)
    ecm_setup_version(${PROJECT_VERSION}
        VARIABLE_PREFIX MD4QT
        VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/config-md4qt.h"
    )

    set(MD4QT_INSTALL_INCLUDEDIR ${KDE_INSTALL_INCLUDEDIR})
    set(MD4QT_INSTALL_BINDIR ${KDE_INSTALL_BINDIR})
    set(MD4QT_INSTALL_LIBDIR ${KDE_INSTALL_LIBDIR})
    set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/md4qt")

    if(INSTALL_MD4QT)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/config-md4qt.h"
                DESTINATION "${MD4QT_INSTALL_INCLUDEDIR}/md4qt")
    endif()
else()
    include(GNUInstallDirs)

    set(MD4QT_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(MD4QT_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(MD4QT_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(CMAKECONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/md4qt")
endif()

if(INSTALL_MD4QT)
    install(FILES ${HDR}
        DESTINATION "${MD4QT_INSTALL_INCLUDEDIR}/md4qt")

    install(TARGETS md4qt
            EXPORT md4qt-targets
            RUNTIME DESTINATION ${MD4QT_INSTALL_BINDIR}
            ARCHIVE DESTINATION ${MD4QT_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${MD4QT_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${MD4QT_INSTALL_INCLUDEDIR}
    )

    install(EXPORT md4qt-targets
            DESTINATION ${CMAKECONFIG_INSTALL_DIR}
            NAMESPACE md4qt::
    )
endif()

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/md4qt-config-version.cmake"
    VERSION ${MD4QT_VERSION}
    COMPATIBILITY AnyNewerVersion)

configure_package_config_file(md4qt-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/md4qt-config.cmake"
    INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR})

if(INSTALL_MD4QT)
    install(FILES ${PROJECT_BINARY_DIR}/md4qt-config.cmake
        ${PROJECT_BINARY_DIR}/md4qt-config-version.cmake
        DESTINATION ${CMAKECONFIG_INSTALL_DIR})
endif()

if(BUILD_MD2HTML_APP)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_executable(md2html utils/md2html/main.cpp)

    target_link_libraries(md2html md4qt::md4qt Qt6::Core)

    if(INSTALL_MD4QT)
        install(TARGETS md2html RUNTIME DESTINATION ${MD4QT_INSTALL_BINDIR})
    endif()
endif()

if(BUILD_MD2QDOC_APP)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_executable(md2qdoc utils/md2qdoc/main.cpp)

    target_link_libraries(md2qdoc md4qt::md4qt Qt6::Core)

    if(INSTALL_MD4QT)
        install(TARGETS md2qdoc RUNTIME DESTINATION ${MD4QT_INSTALL_BINDIR})
    endif()
endif()

if(ECM_FOUND)
    if(ECM_VERSION VERSION_GREATER_EQUAL "6.11.0")
        ecm_generate_qdoc(md4qt docs/md4qt.qdocconf)
    endif()

    kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
endif()

if(NOT ${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
    set(md4qt_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/src PARENT_SCOPE)
    set(md4qt_VERSION_STRING ${MD4QT_VERSION} PARENT_SCOPE)
endif()
