cmake_minimum_required(VERSION 3.24)

project(emergences VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EMERGENCES_BUILD_APP "Build the GLFW / Vulkan application" ON)
option(EMERGENCES_ENABLE_GPU_TESTS "Register Vulkan presentation smoke tests (requires a desktop and GPU)" OFF)
option(EMERGENCES_ENABLE_OPENMP "Use OpenMP for supported CPU rule kernels when available" ON)

include(CTest)
include(cmake/ProjectOptions.cmake)

# Fail before fetching dependencies when the SDK is missing.
if(EMERGENCES_BUILD_APP)
    find_package(Vulkan QUIET COMPONENTS glslc)
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR
            "Vulkan development files were not found. Install the LunarG Vulkan SDK "
            "and set VULKAN_SDK to its root, then configure again. "
            "The GPU driver alone is insufficient. To build only CPU simulation and rules, "
            "configure with -DEMERGENCES_BUILD_APP=OFF.")
    endif()
    find_program(EMERGENCES_GLSLC NAMES glslc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(NOT EMERGENCES_GLSLC AND Vulkan_GLSLC_EXECUTABLE)
        set(EMERGENCES_GLSLC "${Vulkan_GLSLC_EXECUTABLE}")
    endif()
    if(NOT EMERGENCES_GLSLC)
        message(FATAL_ERROR "glslc was not found. Install the shader compiler from the LunarG Vulkan SDK.")
    endif()
elseif(EMERGENCES_ENABLE_GPU_TESTS)
    message(FATAL_ERROR "EMERGENCES_ENABLE_GPU_TESTS requires EMERGENCES_BUILD_APP=ON.")
endif()

include(cmake/Dependencies.cmake)
add_subdirectory(src/config)
add_subdirectory(src/sim)

if(EMERGENCES_BUILD_APP)
    include(cmake/Shaders.cmake)
    add_subdirectory(src/core)
endif()
add_subdirectory(src/scenarios/ants)
add_subdirectory(src/scenarios/particle_life)
add_subdirectory(src/scenarios/boids)
