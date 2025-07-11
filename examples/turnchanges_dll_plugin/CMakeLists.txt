cmake_minimum_required(VERSION 3.20)

project(TurnChangesPlugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories for UEVR headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Source files
set(SOURCES
    Plugin.cpp
)

# Create the DLL
add_library(TurnChangesPlugin SHARED ${SOURCES})

# Link required Windows libraries
target_link_libraries(TurnChangesPlugin 
    PRIVATE
    kernel32
    user32
    xinput
)

# Compiler-specific options
if(MSVC)
    target_compile_options(TurnChangesPlugin PRIVATE /W3)
    # Suppress specific warnings if needed
    target_compile_definitions(TurnChangesPlugin PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(TurnChangesPlugin PRIVATE -Wall -Wextra)
endif()

# Output directory
set_target_properties(TurnChangesPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
) 