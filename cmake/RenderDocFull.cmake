include_guard(GLOBAL)

option(UEVR_BUILD_FULL_RENDERDOC "Build the full RenderDoc runtime alongside UEVR" ON)
option(UEVR_RENDERDOC_ALWAYS_BUILD "Run RenderDoc's MSBuild target whenever the UEVR target builds" ON)
set(UEVR_RENDERDOC_SOURCE_DIR "E:/Github/renderdoc" CACHE PATH "Path to the full RenderDoc source checkout")
set(UEVR_RENDERDOC_CONFIGURATION "Development" CACHE STRING "RenderDoc Visual Studio configuration to build")
set_property(CACHE UEVR_RENDERDOC_CONFIGURATION PROPERTY STRINGS Debug Development Release)
set(UEVR_RENDERDOC_PLATFORM "x64" CACHE STRING "RenderDoc Visual Studio platform to build")
set_property(CACHE UEVR_RENDERDOC_PLATFORM PROPERTY STRINGS x64 Win32)

function(uevr_attach_full_renderdoc target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "uevr_attach_full_renderdoc target does not exist: ${target_name}")
    endif()

    if(NOT UEVR_BUILD_FULL_RENDERDOC)
        message(STATUS "Full RenderDoc build disabled for ${target_name}")
        return()
    endif()

    if(NOT WIN32)
        message(WARNING "Full RenderDoc build bridge is currently Windows/MSBuild only")
        return()
    endif()

    file(TO_CMAKE_PATH "${UEVR_RENDERDOC_SOURCE_DIR}" renderdoc_source_dir)
    set(renderdoc_solution "${renderdoc_source_dir}/renderdoc.sln")
    set(renderdoc_solution_target "DLL\\renderdoc")
    set(renderdoc_output "${renderdoc_source_dir}/${UEVR_RENDERDOC_PLATFORM}/${UEVR_RENDERDOC_CONFIGURATION}/renderdoc.dll")
    set(renderdoc_pdb "${renderdoc_source_dir}/${UEVR_RENDERDOC_PLATFORM}/${UEVR_RENDERDOC_CONFIGURATION}/renderdoc.pdb")

    if(NOT EXISTS "${renderdoc_solution}")
        message(WARNING "RenderDoc solution not found: ${renderdoc_solution}")
        return()
    endif()

    if(CMAKE_VS_MSBUILD_COMMAND)
        set(renderdoc_msbuild "${CMAKE_VS_MSBUILD_COMMAND}")
    else()
        find_program(renderdoc_msbuild MSBuild.exe
            HINTS
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin"
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin"
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin"
                "$ENV{ProgramFiles(x86)}/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin")
    endif()

    if(NOT renderdoc_msbuild)
        message(WARNING "MSBuild.exe was not found; cannot build full RenderDoc")
        return()
    endif()

    set(renderdoc_build_command
        "${renderdoc_msbuild}" "${renderdoc_solution}"
            /t:${renderdoc_solution_target}
            /p:Configuration=${UEVR_RENDERDOC_CONFIGURATION}
            /p:Platform=${UEVR_RENDERDOC_PLATFORM}
            /m
            /v:minimal
            /clp:Summary)

    if(NOT TARGET renderdoc_full AND UEVR_RENDERDOC_ALWAYS_BUILD)
        add_custom_target(renderdoc_full
            COMMAND ${renderdoc_build_command}
            BYPRODUCTS "${renderdoc_output}" "${renderdoc_pdb}"
            WORKING_DIRECTORY "${renderdoc_source_dir}"
            COMMENT "Building full RenderDoc ${UEVR_RENDERDOC_CONFIGURATION}|${UEVR_RENDERDOC_PLATFORM}"
            VERBATIM)
    elseif(NOT TARGET renderdoc_full)
        add_custom_command(
            OUTPUT "${renderdoc_output}"
            COMMAND ${renderdoc_build_command}
            BYPRODUCTS "${renderdoc_pdb}"
            WORKING_DIRECTORY "${renderdoc_source_dir}"
            COMMENT "Building full RenderDoc ${UEVR_RENDERDOC_CONFIGURATION}|${UEVR_RENDERDOC_PLATFORM}"
            VERBATIM)
        add_custom_target(renderdoc_full DEPENDS "${renderdoc_output}")
    endif()

    add_dependencies(${target_name} renderdoc_full)

    target_compile_definitions(${target_name} PRIVATE UEVR_FULL_RENDERDOC_BUILD=1)
    target_include_directories(${target_name} BEFORE PRIVATE "${renderdoc_source_dir}/renderdoc/api/app")

    if(NOT TARGET uevr_renderdoc_launcher)
        add_executable(uevr_renderdoc_launcher
            "${CMAKE_SOURCE_DIR}/tools/renderdoc-launcher/RenderDocLauncher.cpp")
        target_compile_features(uevr_renderdoc_launcher PRIVATE cxx_std_20)
        target_compile_options(uevr_renderdoc_launcher PRIVATE /EHsc /MP)
        set_target_properties(uevr_renderdoc_launcher PROPERTIES
            OUTPUT_NAME "UEVRRenderDocLauncher"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/${target_name}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/${target_name}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/${target_name}")
        add_dependencies(uevr_renderdoc_launcher renderdoc_full)
        add_dependencies(${target_name} uevr_renderdoc_launcher)
        add_custom_command(TARGET uevr_renderdoc_launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${renderdoc_output}"
                    "$<TARGET_FILE_DIR:uevr_renderdoc_launcher>/renderdoc.dll"
            COMMENT "Copying full RenderDoc runtime beside UEVRRenderDocLauncher.exe"
            VERBATIM)
    endif()

    if(NOT TARGET uevr_renderdoc_smoke)
        add_executable(uevr_renderdoc_smoke
            "${CMAKE_SOURCE_DIR}/tools/renderdoc-smoke/D3D12Smoke.cpp")
        target_compile_features(uevr_renderdoc_smoke PRIVATE cxx_std_20)
        target_compile_options(uevr_renderdoc_smoke PRIVATE /EHsc /MP)
        target_link_libraries(uevr_renderdoc_smoke PRIVATE d3d12 dxgi)
        set_target_properties(uevr_renderdoc_smoke PROPERTIES
            OUTPUT_NAME "UEVRRenderDocSmoke"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/${target_name}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/${target_name}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/${target_name}")
        add_dependencies(${target_name} uevr_renderdoc_smoke)
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${renderdoc_output}"
                "$<TARGET_FILE_DIR:${target_name}>/renderdoc.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${renderdoc_pdb}"
                "$<TARGET_FILE_DIR:${target_name}>/renderdoc.pdb"
        COMMENT "Copying full RenderDoc runtime beside $<TARGET_FILE_NAME:${target_name}>"
        VERBATIM)

    message(STATUS "Full RenderDoc build attached to ${target_name}: ${renderdoc_solution}")
endfunction()
