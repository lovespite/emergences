# Build-time compilation; outputs are kept alongside the multi-config executable.
# DEPFILE tracks future GLSL #include dependencies as well as the top-level file.
function(emergences_add_shaders target)
    set(outputs)
    foreach(shader IN LISTS ARGN)
        set(source "${PROJECT_SOURCE_DIR}/shaders/${shader}")
        set(output "${PROJECT_BINARY_DIR}/$<CONFIG>/shaders/${shader}.spv")
        get_filename_component(output_directory "${output}" DIRECTORY)
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
            COMMAND "${EMERGENCES_GLSLC}" --target-env=vulkan1.0
                -I "${PROJECT_SOURCE_DIR}/shaders"
                -MD -MF "${output}.d" "${source}" -o "${output}"
            DEPENDS "${source}"
            DEPFILE "${output}.d"
            COMMENT "Compiling GLSL: ${shader}"
            VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()
    add_custom_target(${target}_shaders DEPENDS ${outputs})
    add_dependencies(${target} ${target}_shaders)
endfunction()
