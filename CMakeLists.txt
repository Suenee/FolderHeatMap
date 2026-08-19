cmake_minimum_required(VERSION 3.24)
project(FolderHeatMap VERSION 1.01 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FolderHeatMap is a Windows Total Commander plugin.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "FolderHeatMap targets Total Commander x64. Configure an x64 build.")
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/vendor/sqlite/sqlite3.c")
    message(FATAL_ERROR "SQLite amalgamation is missing. Run upgrade.cmd first.")
endif()

# WDX 1.01 is intentionally tiny: it only reads a frozen immutable RAM snapshot
# and forwards directory-navigation events to the background engine.
add_library(FolderHeatMap SHARED
    src/FolderHeatMapV101.cpp
)
target_include_directories(FolderHeatMap PRIVATE src)
target_compile_features(FolderHeatMap PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMap PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# All heat math, filesystem work, SQLite access, prediction and persistence live here.
add_executable(FolderHeatMapEngine
    src/EngineApp.cpp
    src/FolderIdentity.cpp
    src/Database.cpp
    src/RuntimeCacheDatabase.cpp
    src/Settings.cpp
    vendor/sqlite/sqlite3.c
)
target_include_directories(FolderHeatMapEngine PRIVATE src vendor/sqlite)
target_compile_features(FolderHeatMapEngine PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMapEngine PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
)

add_dependencies(FolderHeatMap FolderHeatMapEngine)
add_custom_command(TARGET FolderHeatMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:FolderHeatMapEngine>"
        "${CMAKE_SOURCE_DIR}/dist/FolderHeatMapEngine.exe"
)

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

add_executable(FolderHeatMapReset WIN32
    src/ResetApp.cpp
    src/ResetDatabase.cpp
    src/Database.cpp
    src/FolderIdentity.cpp
    vendor/sqlite/sqlite3.c
)
target_include_directories(FolderHeatMapReset PRIVATE src vendor/sqlite)
target_compile_features(FolderHeatMapReset PRIVATE cxx_std_20)
target_compile_definitions(FolderHeatMapReset PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
)
target_link_libraries(FolderHeatMapReset PRIVATE shell32 advapi32)

if(MSVC)
    target_compile_options(FolderHeatMap PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(FolderHeatMapEngine PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(FolderHeatMapConfig PRIVATE /W4 /permissive- /EHsc /utf-8 /FIwindows.h /FIshellapi.h)
    target_compile_options(FolderHeatMapReset PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_source_files_properties(vendor/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "/W0")
endif()

set_target_properties(FolderHeatMap PROPERTIES PREFIX "" SUFFIX ".wdx64" OUTPUT_NAME "FolderHeatMap")
set_target_properties(FolderHeatMapEngine PROPERTIES OUTPUT_NAME "FolderHeatMapEngine")
set_target_properties(FolderHeatMapConfig PROPERTIES OUTPUT_NAME "FolderHeatMapConfig")
set_target_properties(FolderHeatMapReset PROPERTIES OUTPUT_NAME "FolderHeatMapReset")
