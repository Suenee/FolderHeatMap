cmake_minimum_required(VERSION 3.24)
project(FolderHeatMap VERSION 0.3.0 LANGUAGES C CXX)

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
    src/Settings.cpp
    vendor/sqlite/sqlite3.c
)

target_include_directories(FolderHeatMap PRIVATE src vendor/sqlite)
target_compile_features(FolderHeatMap PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMap PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
)

# The approved configurator artwork is stored as Base64 so it can live in the
# repository through text-only tooling. Decode it during configure and embed it
# as the native Windows executable/window icon.
set(FHM_CONFIG_ICON_B64 "${CMAKE_SOURCE_DIR}/assets/FolderHeatMapConfig.ico.b64")
set(FHM_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(FHM_CONFIG_ICON "${FHM_GENERATED_DIR}/FolderHeatMapConfig.ico")
set(FHM_CONFIG_RC "${FHM_GENERATED_DIR}/FolderHeatMapConfig.rc")
file(MAKE_DIRECTORY "${FHM_GENERATED_DIR}")
execute_process(
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -Command
        "[IO.File]::WriteAllBytes('${FHM_CONFIG_ICON}', [Convert]::FromBase64String([IO.File]::ReadAllText('${FHM_CONFIG_ICON_B64}').Trim()))"
    RESULT_VARIABLE FHM_ICON_DECODE_RESULT
)
if(NOT FHM_ICON_DECODE_RESULT EQUAL 0 OR NOT EXISTS "${FHM_CONFIG_ICON}")
    message(FATAL_ERROR "Could not decode FolderHeatMap configurator icon.")
endif()
file(WRITE "${FHM_CONFIG_RC}" "IDI_FHM_CONFIG ICON \"${FHM_CONFIG_ICON}\"\n")

add_executable(FolderHeatMapConfig WIN32
    src/ConfigAppCompile.cpp
    src/Settings.cpp
    "${FHM_CONFIG_RC}"
)
target_include_directories(FolderHeatMapConfig PRIVATE src)
target_compile_features(FolderHeatMapConfig PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMapConfig PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(FolderHeatMapConfig PRIVATE comdlg32 shell32 advapi32 comctl32)

if(MSVC)
    target_compile_options(FolderHeatMap PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(FolderHeatMapConfig PRIVATE /W4 /permissive- /EHsc /utf-8 /FIwindows.h /FIshellapi.h)
    set_source_files_properties(vendor/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "/W0")
endif()

set_target_properties(FolderHeatMap PROPERTIES PREFIX "" SUFFIX ".wdx64" OUTPUT_NAME "FolderHeatMap")
set_target_properties(FolderHeatMapConfig PROPERTIES OUTPUT_NAME "FolderHeatMapConfig")
