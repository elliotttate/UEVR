cmake_minimum_required(VERSION 3.16)
project(FPSPlotterPlugin)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find UEVR paths
set(UEVR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(UEVR_INCLUDE "${UEVR_ROOT}/include")
set(UEVR_EXAMPLES "${UEVR_ROOT}/examples")
set(UEVR_SRC "${UEVR_ROOT}/src")

# Include directories
include_directories(
    "${UEVR_INCLUDE}"
    "${UEVR_EXAMPLES}/renderlib"
    "${UEVR_EXAMPLES}/renderlib/imgui"
    "${UEVR_EXAMPLES}/renderlib/rendering"
    "${UEVR_SRC}/uevr-imgui"
    "${UEVR_ROOT}/dependencies/submodules/imgui"
)

# Source files
set(SOURCES
    Plugin.cpp
)

# Create shared library
add_library(FPSPlotterPlugin SHARED ${SOURCES})

# Link libraries
target_link_libraries(FPSPlotterPlugin
    d3d11
    d3d12
    dxgi
    d3dcompiler
)

# Set output directory
set_target_properties(FPSPlotterPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
) 