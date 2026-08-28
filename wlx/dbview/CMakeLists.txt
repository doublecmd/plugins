cmake_minimum_required(VERSION 3.20)
project(dbview_qt6 CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Allow older cmake_minimum_required in FetchContent dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# Optional engine flags
option(ENABLE_ROCKSDB_LEVELDB "Enable RocksDB and LevelDB support using RocksDB (large dependency)" OFF)
if(ENABLE_ROCKSDB_LEVELDB)
    add_compile_options(-include stdint.h)
endif()

# NOTE: no Sql component — SqliteEngine now talks to its database directly
# (vendored libsqlite3) instead of going through Qt6::Sql + the QSQLITE
# driver plugin. This removes a runtime dependency on the Qt6 SQL module
# and its driver plugins entirely.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Ensure the platform static library is built with -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Pull in the platform library (builds libwlxbase_wlqt.a)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_wlqt
    ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_wlqt
)

# ---------------------------------------------------------------------------
# External dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)

# --- DuckDB ---
set(BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTENSIONS "parquet" CACHE STRING "" FORCE)
set(EXTENSION_STATIC_BUILD ON CACHE BOOL "" FORCE)
set(ENABLE_EXTENSION_AUTOLOADING OFF CACHE BOOL "" FORCE)
set(ENABLE_EXTENSION_AUTOINSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(duckdb
    GIT_REPOSITORY https://github.com/duckdb/duckdb.git
    GIT_TAG        v1.3.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(duckdb)

# --- RocksDB (optional) ---
if(ENABLE_ROCKSDB_LEVELDB)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARK_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
    set(ROCKSDB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(FAIL_ON_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_RTTI 1 CACHE STRING "" FORCE)
    FetchContent_Declare(rocksdb
        GIT_REPOSITORY https://github.com/facebook/rocksdb.git
        GIT_TAG        v9.11.2
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  sed -i "1i #include <cstdint>" db/blob/blob_file_meta.h
    )
    FetchContent_MakeAvailable(rocksdb)
endif()

# --- MS Access static libmdb ---
add_library(mdb STATIC
    src/libmdb/catalog.c
    src/libmdb/file.c
    src/libmdb/table.c
    src/libmdb/data.c
    src/libmdb/dump.c
    src/libmdb/backend.c
    src/libmdb/money.c
    src/libmdb/sargs.c
    src/libmdb/index.c
    src/libmdb/like.c
    src/libmdb/write.c
    src/libmdb/stats.c
    src/libmdb/map.c
    src/libmdb/props.c
    src/libmdb/worktable.c
    src/libmdb/options.c
    src/libmdb/iconv.c
    src/libmdb/version.c
    src/libmdb/rc4.c
    src/libmdb/fakeglib.c
)
target_include_directories(mdb PRIVATE
    include/mdbtools
)
target_compile_definitions(mdb PRIVATE FAKE_GLIB=1 "TLS=__thread" HAVE_ICONV=1 "ICONV_CONST=")

# --- Vendored LMDB (Symas OpenLDAP Public License, permissive) ---
# Source pinned at tag LMDB_1.0.1 from https://github.com/LMDB/lmdb, vendored
# directly under src/liblmdb/ (same pattern as libmdb above) so LmdbEngine has
# zero dependency on a system-installed liblmdb — previously this linked
# against whatever liblmdb.so/.a the build machine happened to have, which is
# not guaranteed present on the target machine (confirmed absent from the
# gtk-dev container, for example).
add_library(lmdb STATIC
    src/liblmdb/mdb.c
    src/liblmdb/midl.c
)
target_include_directories(lmdb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/liblmdb
)

# --- Vendored SQLite (public domain) ---
# Amalgamation source (single sqlite3.c/.h pair), vendored directly under
# src/libsqlite3/ from the official sqlite.org release matching the version
# already on this host (3.53.4 / 3530400), so SqliteEngine has zero
# dependency on a system-installed libsqlite3 — previously SQLite support
# went through Qt's QSqlDatabase/QSQLITE driver plugin instead of a direct
# library at all (see SqliteEngine.cpp), a bigger runtime dependency than a
# bare libsqlite3.
add_library(sqlite3 STATIC
    src/libsqlite3/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libsqlite3
)
target_compile_definitions(sqlite3 PRIVATE
    _GNU_SOURCE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_OMIT_LOAD_EXTENSION
)
find_package(Threads REQUIRED)
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- Vendored Berkeley DB (Sleepycat-derived permissive license, NOT AGPL) ---
#
# IMPORTANT license note: the "db"/libdb currently resolved from the system
# linker on a typical modern Linux distro (e.g. Arch/CachyOS's `db` package,
# version 6.2.32) is Oracle's post-6.0 release, licensed AGPL-3.0-only.
# Statically linking that version would place this whole plugin binary under
# AGPL's copyleft/network-use terms. Debian/Ubuntu deliberately stayed on the
# 5.3.x line for the same reason (confirmed: gtk-dev container ships only
# libdb-5.3.so). This build therefore vendors and statically links
# **db-5.3.28**, the last release under the original permissive Sleepycat
# license — verified from LICENSE inside the fetched tarball. Do not bump
# this past the 5.3.x line without re-checking the license of whatever
# version replaces it.
include(ExternalProject)
set(BDB_VERSION "5.3.28")
set(BDB_PREFIX  "${CMAKE_CURRENT_BINARY_DIR}/bdb")
# Berkeley DB's own build system is not CMake-based and must be configured
# and built from its build_unix/ subdirectory (it uses paths relative to
# there, e.g. `../dist/configure`) — hence BUILD_IN_SOURCE 1 plus explicit
# `cd build_unix &&` in each step, run through a shell.
ExternalProject_Add(bdb_ext
    URL               https://download.oracle.com/berkeley-db/db-${BDB_VERSION}.tar.gz
    URL_HASH          SHA256=e0a992d740709892e81f9d93f06daf305cf73fb81b545afe72478043172c3628
    PREFIX            ${BDB_PREFIX}
    BUILD_IN_SOURCE   1
    # CFLAGS below downgrade -Wincompatible-pointer-types / -Wimplicit-int /
    # -Wint-conversion from hard errors back to warnings: GCC 14+ promoted
    # these to errors by default, and BDB 5.3.28's 2013-era K&R-style C
    # (old-style function pointer assignments throughout env_config.c etc.)
    # does not build without this on a current toolchain. Verified against
    # this exact tarball with GCC 16 before wiring into CMake.
    CONFIGURE_COMMAND /bin/sh -c "cd build_unix && CFLAGS='-fPIC -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-return-mismatch -Wno-old-style-definition' \
                          ../dist/configure \
                          --prefix=${BDB_PREFIX}/install \
                          --enable-static --disable-shared \
                          --disable-cxx --disable-tcl --disable-java --disable-replication \
                          --with-mutex=POSIX/pthreads/library"
    BUILD_COMMAND     /bin/sh -c "cd build_unix && make library_build"
    INSTALL_COMMAND   /bin/sh -c "cd build_unix && make install_setup install_include install_lib"
    BUILD_BYPRODUCTS  ${BDB_PREFIX}/install/lib/libdb.a
)

# CMake validates INTERFACE_INCLUDE_DIRECTORIES on IMPORTED targets exist at
# *configure* time, but this directory is only populated later by bdb_ext's
# own build/install step — pre-create it as an empty placeholder so
# configure succeeds; add_dependencies(db bdb_ext) below still guarantees
# the real headers land there before anything actually compiles against it.
file(MAKE_DIRECTORY "${BDB_PREFIX}/install/include")

add_library(db STATIC IMPORTED GLOBAL)
set_target_properties(db PROPERTIES
    IMPORTED_LOCATION "${BDB_PREFIX}/install/lib/libdb.a"
)
target_include_directories(db INTERFACE "${BDB_PREFIX}/install/include")
add_dependencies(db bdb_ext)

# ---------------------------------------------------------------------------
# Plugin library
# ---------------------------------------------------------------------------
# NOTE: Headers must be listed explicitly so AUTOMOC processes them.
# RocksDbEngine.h is excluded when ENABLE_ROCKSDB is OFF to prevent MOC
# from generating code that references uncompiled RocksDbEngine symbols.
set(DBVIEW_HEADERS
    include/DbEngine.h
    include/DbViewWidget.h
    include/DuckDbEngine.h
    include/DuckDbModel.h
    include/KeyValueModel.h
    include/SqliteEngine.h
    include/SqliteTableModel.h
    include/MdbEngine.h
    include/BdbEngine.h
    include/LmdbEngine.h
)

set(DBVIEW_SOURCES
    ${DBVIEW_HEADERS}
    src/wlx_entry.cpp
    src/DbEngine.cpp
    src/DbViewWidget.cpp
    src/SqliteEngine.cpp
    src/SqliteTableModel.cpp
    src/DuckDbEngine.cpp
    src/DuckDbModel.cpp
    src/KeyValueModel.cpp
    src/MdbEngine.cpp
    src/BdbEngine.cpp
    src/LmdbEngine.cpp
)

if(ENABLE_ROCKSDB_LEVELDB)
    list(APPEND DBVIEW_SOURCES
        include/LevelDbEngine.h src/LevelDbEngine.cpp
        include/RocksDbEngine.h src/RocksDbEngine.cpp
    )
endif()

add_library(dbview_qt6 SHARED ${DBVIEW_SOURCES})

if(ENABLE_ROCKSDB_LEVELDB)
    target_compile_definitions(dbview_qt6 PRIVATE ENABLE_ROCKSDB_LEVELDB)
endif()

set_target_properties(dbview_qt6 PROPERTIES
    PREFIX ""
    SUFFIX ".wlx"
)

target_include_directories(dbview_qt6 PRIVATE
    include
    include/mdbtools
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    ${duckdb_SOURCE_DIR}/src/include
)

if(ENABLE_ROCKSDB_LEVELDB)
    target_include_directories(dbview_qt6 PRIVATE
        ${rocksdb_SOURCE_DIR}/include
    )
endif()

# NOTE: duckdb_static (not the shared "duckdb" target) is linked deliberately —
# the shared target produced a dbview_qt6.wlx that referenced libduckdb.so at a
# raw build-tree path (verified via ldd), which would not resolve on another
# machine at all. duckdb_static makes DuckDB support fully self-contained.
set(DBVIEW_LINK_LIBS
    wlxbase_wlqt
    Qt6::Widgets
    Qt6::Gui
    Qt6::Core
    duckdb_static
    mdb
    sqlite3
    lmdb
    db
)

if(ENABLE_ROCKSDB_LEVELDB)
    list(APPEND DBVIEW_LINK_LIBS rocksdb)
endif()

target_link_libraries(dbview_qt6 PRIVATE ${DBVIEW_LINK_LIBS})

# Explicit build-order dependency: add_dependencies() on the IMPORTED "db"
# target alone is not reliably propagated to consumers across CMake/generator
# versions, so state it directly here too.
add_dependencies(dbview_qt6 bdb_ext)

# ---------------------------------------------------------------------------
# Qt-free core (src/core/) + GTK3 UI
# ---------------------------------------------------------------------------
option(ENABLE_GTK3 "Build the GTK3 dbview plugin" ON)

if(ENABLE_GTK3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(GTK3_FOUND)
        set(DBVIEW_CORE_SOURCES
            src/core/DbEngineCoreFactory.cpp
            src/core/SqliteEngineCore.cpp
            src/core/DuckDbEngineCore.cpp
            src/core/LmdbEngineCore.cpp
            src/core/BdbEngineCore.cpp
            src/core/MdbEngineCore.cpp
            src/core/RocksDbEngineCore.cpp
            src/core/LevelDbEngineCore.cpp
        )

        add_library(dbview_core STATIC ${DBVIEW_CORE_SOURCES})
        target_include_directories(dbview_core PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/include/mdbtools
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${duckdb_SOURCE_DIR}/src/include
        )
        if(ENABLE_ROCKSDB_LEVELDB)
            target_include_directories(dbview_core PUBLIC ${rocksdb_SOURCE_DIR}/include)
            target_compile_definitions(dbview_core PUBLIC ENABLE_ROCKSDB_LEVELDB)
        endif()
        set(DBVIEW_CORE_LINK_LIBS duckdb_static mdb sqlite3 lmdb db)
        if(ENABLE_ROCKSDB_LEVELDB)
            list(APPEND DBVIEW_CORE_LINK_LIBS rocksdb)
        endif()
        target_link_libraries(dbview_core PUBLIC ${DBVIEW_CORE_LINK_LIBS})
        add_dependencies(dbview_core bdb_ext)

        add_library(dbview_gtk3 SHARED src/gtk3/plugin_gtk3.cpp)
        set_target_properties(dbview_gtk3 PROPERTIES PREFIX "" SUFFIX ".wlx")
        target_include_directories(dbview_gtk3 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
            ${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk/include
            ${GTK3_INCLUDE_DIRS}
        )
        target_link_directories(dbview_gtk3 PRIVATE ${GTK3_LIBRARY_DIRS})
        target_compile_options(dbview_gtk3 PRIVATE ${GTK3_CFLAGS_OTHER})
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../wlxbase_gtk ${CMAKE_CURRENT_BINARY_DIR}/wlxbase_gtk)
        target_link_libraries(dbview_gtk3 PRIVATE
            dbview_core
            wlxbase_gtk
            ${GTK3_LIBRARIES}
        )
        # See officeview's CMakeLists.txt for why: statically-linked
        # third-party libs (sqlite3/lmdb/db/duckdb's own bundled deps) get
        # exported from the .wlx by default and can be interposed by
        # whatever the host process already loaded of the same library.
        target_link_options(dbview_gtk3 PRIVATE -Wl,-Bsymbolic -Wl,-Bsymbolic-functions)
    else()
        message(WARNING "dbview: GTK3 (gtk+-3.0) not found, skipping GTK3 build")
    endif()
endif()
