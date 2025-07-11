cmake_minimum_required(VERSION 3.20)
project(ImGuiHookPlugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add plugin source
set(PLUGIN_SOURCES
    Plugin.cpp
)

# Create the plugin DLL
add_library(ImGuiHookPlugin SHARED ${PLUGIN_SOURCES})

# Set output name
set_target_properties(ImGuiHookPlugin PROPERTIES OUTPUT_NAME "ImGuiHookPlugin")

# Include directories
target_include_directories(ImGuiHookPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../UEVR3/include
)

# Find required libraries
find_library(DETOURS_LIB detours)
find_library(DETOURS_LIB_DEBUG detours_debug)

# If Detours is not found, try to find it in common locations
if(NOT DETOURS_LIB)
    # Try to find Detours in vcpkg
    find_path(DETOURS_INCLUDE_DIR detours.h
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/Microsoft.Detours/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/detours/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/installed/x64-windows/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-windows/include
        $ENV{VCPKG_ROOT}/installed/x64-windows/include
        "C:/vcpkg/installed/x64-windows/include"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/vcpkg/installed/x64-windows/include"
    )

    find_library(DETOURS_LIB detours
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/Microsoft.Detours/lib/x64
        ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/detours/lib/x64
        ${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/installed/x64-windows/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-windows/lib
        $ENV{VCPKG_ROOT}/installed/x64-windows/lib
        "C:/vcpkg/installed/x64-windows/lib"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/vcpkg/installed/x64-windows/lib"
    )

    if(DETOURS_INCLUDE_DIR AND DETOURS_LIB)
        target_include_directories(ImGuiHookPlugin PRIVATE ${DETOURS_INCLUDE_DIR})
        message(STATUS "Found Detours: ${DETOURS_LIB}")
    else()
        message(WARNING "Detours not found! Install via vcpkg: vcpkg install detours:x64-windows")
        # Create a fallback target without Detours
        target_compile_definitions(ImGuiHookPlugin PRIVATE NO_DETOURS)
    endif()
endif()

# Link libraries
if(DETOURS_LIB)
    target_link_libraries(ImGuiHookPlugin PRIVATE ${DETOURS_LIB})
endif()

# Link system libraries
target_link_libraries(ImGuiHookPlugin PRIVATE
    user32
    kernel32
    xinput
)

# Compiler flags
target_compile_definitions(ImGuiHookPlugin PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# Set runtime library
set_property(TARGET ImGuiHookPlugin PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Output directory
set_target_properties(ImGuiHookPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Additional compiler flags for optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ImGuiHookPlugin PRIVATE
        /O2
        /GL
        /DNDEBUG
    )
    target_link_options(ImGuiHookPlugin PRIVATE
        /LTCG
        /OPT:REF
        /OPT:ICF
    )
endif()

# Copy plugin to output directory
add_custom_command(TARGET ImGuiHookPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:ImGuiHookPlugin>
    "${CMAKE_BINARY_DIR}/plugins/ImGuiHookPlugin.dll"
    COMMENT "Copying ImGuiHookPlugin.dll to plugins directory"
)

# Install target
install(TARGETS ImGuiHookPlugin
    RUNTIME DESTINATION plugins
    LIBRARY DESTINATION plugins
) 