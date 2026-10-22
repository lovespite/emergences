add_library(emergences_boids STATIC Alignment.cpp Cohesion.cpp Separation.cpp
    BoidsScene.cpp CursorIntervention.cpp CursorRepel.cpp CursorAttract.cpp)
add_library(emergences::boids ALIAS emergences_boids)
target_link_libraries(emergences_boids PUBLIC emergences::sim)
emergences_target_defaults(emergences_boids)

if(BUILD_TESTING)
    set(header_checks)
    foreach(header Alignment Cohesion Separation BoidsScene CursorIntervention CursorRepel CursorAttract)
        set(check "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${header}.cpp")
        file(GENERATE OUTPUT "${check}" CONTENT "#include \"scenarios/boids/${header}.h\"\n")
        list(APPEND header_checks "${check}")
    endforeach()
    add_library(emergences_boids_header_check OBJECT ${header_checks})
    target_link_libraries(emergences_boids_header_check PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_header_check)

    add_executable(emergences_boids_rule_tests tests/BoidsRuleTests.cpp)
    target_link_libraries(emergences_boids_rule_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_rule_tests)
    add_test(NAME boids_rules COMMAND emergences_boids_rule_tests)

    add_executable(emergences_boids_simulation_tests tests/BoidsSimulationTests.cpp)
    target_link_libraries(emergences_boids_simulation_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_simulation_tests)
    add_test(NAME boids_simulation COMMAND emergences_boids_simulation_tests)
    add_executable(emergences_cursor_tests tests/CursorInterventionTests.cpp)
    target_link_libraries(emergences_cursor_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_cursor_tests)
    add_test(NAME boids_cursor COMMAND emergences_cursor_tests)

    add_executable(emergences_boids_scene_tests tests/BoidsSceneTests.cpp)
    target_link_libraries(emergences_boids_scene_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_scene_tests)
    add_test(NAME boids_scene COMMAND emergences_boids_scene_tests)
    set_tests_properties(boids_rules boids_simulation boids_cursor boids_scene
        PROPERTIES LABELS "cpu;sim;boids" TIMEOUT 30)
endif()

if(EMERGENCES_BUILD_APP)
    add_executable(emergences main.cpp BoidsApp.cpp LiveBoids.cpp RenderPreview.cpp)
    target_link_libraries(emergences PRIVATE emergences::core emergences::boids emergences::imgui)
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
        add_test(NAME vulkan_boids COMMAND emergences --boids-smoke-test)
        set_tests_properties(vulkan_triangle vulkan_particles vulkan_boids PROPERTIES
            TIMEOUT 60 LABELS "gpu;smoke"
            RUN_SERIAL TRUE
            FAIL_REGULAR_EXPRESSION "\\[Vulkan validation\\];Validation Error")
    endif()
endif()
