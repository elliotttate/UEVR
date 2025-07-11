cmake_minimum_required(VERSION 3.24)
project(AltDPadPlugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Plugin source files
set(PLUGIN_SOURCES
    Plugin.cpp
)

# Create the plugin DLL
add_library(AltDPadPlugin SHARED ${PLUGIN_SOURCES})

# Set output directory
set_target_properties(AltDPadPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# Include UEVR headers
target_include_directories(AltDPadPlugin PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

# Link required libraries
target_link_libraries(AltDPadPlugin PRIVATE
    psapi        # For process and module information
    user32       # For Windows API
    kernel32     # For Windows API
)

# Compiler-specific settings
if(MSVC)
    target_compile_options(AltDPadPlugin PRIVATE
        /W4          # High warning level
        /EHsc        # Exception handling
        /MP          # Multi-processor compilation
    )
    target_compile_definitions(AltDPadPlugin PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# Debug configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(AltDPadPlugin PRIVATE
        _DEBUG
        DEBUG
    )
endif()

# Set the output name
set_target_properties(AltDPadPlugin PROPERTIES
    OUTPUT_NAME "AltDPadPlugin"
)

message(STATUS "AltDPadPlugin: Memory patch plugin configured")
message(STATUS "AltDPadPlugin: Output directory: ${CMAKE_BINARY_DIR}/bin") 