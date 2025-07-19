cmake_minimum_required(VERSION 3.24)
project(ButtonRemapperPlugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find UEVR headers - try multiple possible locations
set(POSSIBLE_UEVR_PATHS
    "../include"
    "../UEVR3/include"
    "../../include"
    "../../../include"
)

foreach(PATH ${POSSIBLE_UEVR_PATHS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${PATH}/uevr/Plugin.hpp")
        set(UEVR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${PATH}")
        break()
    endif()
endforeach()

if(NOT UEVR_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find UEVR headers. Please ensure uevr/Plugin.hpp is available in one of the expected paths.")
endif()

message(STATUS "Found UEVR headers at: ${UEVR_INCLUDE_DIR}")

# Find renderlib - try multiple possible locations
set(POSSIBLE_RENDERLIB_PATHS
    "../examples/renderlib"
    "../UEVR3/examples/renderlib"
    "../../examples/renderlib"
    "../../../examples/renderlib"
    "../renderlib"
)

foreach(PATH ${POSSIBLE_RENDERLIB_PATHS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${PATH}/rendering/d3d11.hpp")
        set(RENDERLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${PATH}")
        break()
    endif()
endforeach()

if(NOT RENDERLIB_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find renderlib. Please ensure renderlib/rendering/d3d11.hpp is available.")
endif()

message(STATUS "Found renderlib at: ${RENDERLIB_INCLUDE_DIR}")

# Find imgui sources
set(IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui/imgui_widgets.cpp
)

# Add the plugin with renderlib sources
add_library(ButtonRemapperPlugin SHARED
    Plugin.cpp
    ${RENDERLIB_INCLUDE_DIR}/rendering/d3d11.cpp
    ${RENDERLIB_INCLUDE_DIR}/rendering/d3d12.cpp
    ${RENDERLIB_INCLUDE_DIR}/imgui/imgui_impl_dx11.cpp
    ${RENDERLIB_INCLUDE_DIR}/imgui/imgui_impl_dx12.cpp
    ${RENDERLIB_INCLUDE_DIR}/imgui/imgui_impl_win32.cpp
    ${IMGUI_SOURCES}
)

# Include directories
target_include_directories(ButtonRemapperPlugin PRIVATE
    ${UEVR_INCLUDE_DIR}
    ${RENDERLIB_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/submodules
)

# Link libraries
target_link_libraries(ButtonRemapperPlugin PRIVATE
    d3d11
    d3d12
    dxgi
    user32
    kernel32
    xinput
)

# Set output directory
set_target_properties(ButtonRemapperPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)