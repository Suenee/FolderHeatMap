cmake_minimum_required(VERSION 3.24)
project(FolderHeatMap VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FolderHeatMap is a Windows Total Commander plugin.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The first FolderHeatMap target is Total Commander x64. Configure an x64 build.")
endif()

add_library(FolderHeatMap SHARED
    src/FolderHeatMap.cpp
    src/FolderIdentity.cpp
)

target_include_directories(FolderHeatMap PRIVATE src)
target_compile_features(FolderHeatMap PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMap PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(FolderHeatMap PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(FolderHeatMap PROPERTIES
    PREFIX ""
    SUFFIX ".wdx64"
    OUTPUT_NAME "FolderHeatMap"
)
