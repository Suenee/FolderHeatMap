cmake_minimum_required(VERSION 3.24)
project(FolderHeatMap VERSION 0.2.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FolderHeatMap is a Windows Total Commander plugin.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "FolderHeatMap targets Total Commander x64. Configure an x64 build.")
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/vendor/sqlite/sqlite3.c")
    message(FATAL_ERROR "SQLite amalgamation is missing. Run upgrade.cmd first.")
endif()

add_library(FolderHeatMap SHARED
    src/FolderHeatMap.cpp
    src/FolderIdentity.cpp
    src/Database.cpp
    vendor/sqlite/sqlite3.c
)

target_include_directories(FolderHeatMap PRIVATE
    src
    vendor/sqlite
)

target_compile_features(FolderHeatMap PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMap PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
)

if(MSVC)
    target_compile_options(FolderHeatMap PRIVATE /W4 /permissive- /EHsc)
    set_source_files_properties(vendor/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "/W0")
endif()

set_target_properties(FolderHeatMap PROPERTIES
    PREFIX ""
    SUFFIX ".wdx64"
    OUTPUT_NAME "FolderHeatMap"
)
