add_library(emergences_boids STATIC Alignment.cpp Cohesion.cpp Separation.cpp
    BoidsScene.cpp BoidsConfig.cpp BoidsRuleKernel.cpp BoidsGpu.cpp CursorIntervention.cpp CursorRepel.cpp CursorAttract.cpp)
add_library(emergences::boids ALIAS emergences_boids)
target_link_libraries(emergences_boids PUBLIC emergences::sim emergences::config)
emergences_target_defaults(emergences_boids)

if(BUILD_TESTING)
    set(header_checks)
    foreach(header Alignment Cohesion Separation BoidsRuleKernel BoidsScene BoidsConfig BoidsGpu CursorIntervention CursorRepel CursorAttract)
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
    add_executable(emergences_boids_kernel_tests tests/BoidsKernelTests.cpp)
    target_link_libraries(emergences_boids_kernel_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_kernel_tests)
    add_test(NAME boids_kernel COMMAND emergences_boids_kernel_tests)
    add_executable(emergences_cursor_tests tests/CursorInterventionTests.cpp)
    target_link_libraries(emergences_cursor_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_cursor_tests)
    add_test(NAME boids_cursor COMMAND emergences_cursor_tests)

    add_executable(emergences_boids_scene_tests tests/BoidsSceneTests.cpp)
    target_link_libraries(emergences_boids_scene_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_scene_tests)
    add_test(NAME boids_scene COMMAND emergences_boids_scene_tests)
    add_executable(emergences_boids_backend_tests tests/BoidsBackendTests.cpp)
    target_link_libraries(emergences_boids_backend_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_backend_tests)
    add_test(NAME boids_backend COMMAND emergences_boids_backend_tests)
    add_executable(emergences_boids_config_tests tests/BoidsConfigTests.cpp)
    target_link_libraries(emergences_boids_config_tests PRIVATE emergences::boids)
    emergences_target_defaults(emergences_boids_config_tests)
    add_test(NAME boids_config COMMAND emergences_boids_config_tests)
    set_tests_properties(boids_rules boids_simulation boids_kernel boids_cursor boids_scene boids_backend boids_config
        PROPERTIES LABELS "cpu;sim;boids" TIMEOUT 30)
endif()

if(EMERGENCES_BUILD_APP)
    add_executable(emergences main.cpp BoidsApp.cpp LiveBoids.cpp RenderPreview.cpp
        ../ants/AntPresentation.cpp ../particle_life/ParticleLifePresentation.cpp)
    if(WIN32)
        set_target_properties(emergences PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_libraries(emergences PRIVATE user32 shell32)
        if(MSVC)
            # Retain normal main/argv and CRT initialization with the GUI subsystem.
            target_link_options(emergences PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
    target_link_libraries(emergences PRIVATE emergences::core emergences::boids emergences::ants
        emergences::particle_life emergences::imgui)
    target_compile_definitions(emergences PRIVATE
        EMERGENCES_VERSION="${PROJECT_VERSION}"
        EMERGENCES_WINDOW_TITLE_PREFIX="Emergences v${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} - "
        EMERGENCES_SHADER_DIRECTORY="$<TARGET_FILE_DIR:emergences>/shaders")
    set_target_properties(emergences PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:emergences>")
    emergences_target_defaults(emergences)
    emergences_add_shaders(emergences smoke/triangle.vert smoke/triangle.frag
        particle.vert particle.frag grid.vert grid.frag compute/boids.comp compute/particle_life.comp)

    # Run even when no C++ source changed, so the distributed reference stays
    # current after documentation edits. Never overwrite the user's .conf file.
    add_custom_target(emergences_config_sample
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/emergences.conf.example"
            "${PROJECT_BINARY_DIR}/$<CONFIG>/emergences.conf.example"
        DEPENDS "${PROJECT_SOURCE_DIR}/emergences.conf.example"
        BYPRODUCTS "${PROJECT_BINARY_DIR}/$<CONFIG>/emergences.conf.example"
        COMMENT "Copying annotated configuration sample"
        VERBATIM)
    add_dependencies(emergences emergences_config_sample)

    if(BUILD_TESTING AND EMERGENCES_ENABLE_GPU_TESTS)
        add_test(NAME vulkan_triangle COMMAND emergences --smoke-test)
        add_test(NAME vulkan_particles COMMAND emergences --render-smoke-test)
        add_test(NAME vulkan_boids COMMAND emergences --boids-smoke-test)
        add_test(NAME vulkan_gpu_boids COMMAND emergences --gpu-smoke-test)
        add_test(NAME vulkan_ants COMMAND emergences --ants-smoke-test)
        add_test(NAME vulkan_particle_life COMMAND emergences --particle-life-smoke-test)
        set_tests_properties(vulkan_triangle vulkan_particles vulkan_boids vulkan_gpu_boids vulkan_ants vulkan_particle_life PROPERTIES
            TIMEOUT 60 LABELS "gpu;smoke"
            RUN_SERIAL TRUE
            FAIL_REGULAR_EXPRESSION "\\[Vulkan validation\\];Validation Error")
        add_executable(emergences_gpu_tests tests/GpuSimulationTests.cpp)
        target_link_libraries(emergences_gpu_tests PRIVATE emergences::core emergences::boids)
        emergences_target_defaults(emergences_gpu_tests)
        target_compile_definitions(emergences_gpu_tests PRIVATE
            EMERGENCES_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/$<CONFIG>/shaders")
        add_dependencies(emergences_gpu_tests emergences_shaders)
        add_test(NAME boids_gpu_compute COMMAND emergences_gpu_tests)
        set_tests_properties(boids_gpu_compute PROPERTIES TIMEOUT 120 LABELS "gpu;sim;boids"
            RUN_SERIAL TRUE FAIL_REGULAR_EXPRESSION "\\[Vulkan validation\\];Validation Error")

        add_executable(emergences_particle_life_gpu_tests ../particle_life/tests/ParticleLifeGpuTests.cpp)
        target_link_libraries(emergences_particle_life_gpu_tests PRIVATE emergences::core emergences::particle_life)
        emergences_target_defaults(emergences_particle_life_gpu_tests)
        target_compile_definitions(emergences_particle_life_gpu_tests PRIVATE
            EMERGENCES_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/$<CONFIG>/shaders")
        add_dependencies(emergences_particle_life_gpu_tests emergences_shaders)
        add_test(NAME particle_life_gpu_compute COMMAND emergences_particle_life_gpu_tests)
        set_tests_properties(particle_life_gpu_compute PROPERTIES TIMEOUT 120 LABELS "gpu;sim;particle_life"
            RUN_SERIAL TRUE FAIL_REGULAR_EXPRESSION "\\[Vulkan validation\\];Validation Error")
    endif()
endif()
