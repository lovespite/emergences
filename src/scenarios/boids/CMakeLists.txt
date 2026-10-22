add_executable(emergences main.cpp BoidsApp.cpp RenderPreview.cpp)
target_link_libraries(emergences PRIVATE emergences::core emergences::sim emergences::imgui)
target_compile_definitions(emergences PRIVATE
    EMERGENCES_VERSION="${PROJECT_VERSION}"
    EMERGENCES_SHADER_DIRECTORY="$<TARGET_FILE_DIR:emergences>/shaders")
set_target_properties(emergences PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:emergences>")
emergences_target_defaults(emergences)
emergences_add_shaders(emergences smoke/triangle.vert smoke/triangle.frag
    particle.vert particle.frag grid.vert grid.frag)

if(BUILD_TESTING AND EMERGENCES_ENABLE_GPU_TESTS)
    add_test(NAME vulkan_triangle COMMAND emergences --smoke-test)
    add_test(NAME vulkan_particles COMMAND emergences --render-smoke-test)
    set_tests_properties(vulkan_triangle vulkan_particles PROPERTIES
        TIMEOUT 30 LABELS "gpu;smoke"
        RUN_SERIAL TRUE
        FAIL_REGULAR_EXPRESSION "\\[Vulkan validation\\];Validation Error")
endif()
