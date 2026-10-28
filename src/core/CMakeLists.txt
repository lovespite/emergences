add_library(emergences_core STATIC
    Window.cpp Context.cpp Swapchain.cpp Pipeline.cpp FrameRenderer.cpp TriangleRenderer.cpp
    Buffer.cpp Descriptor.cpp ComputeDevice.cpp Camera.cpp ParticleRenderer.cpp LineRenderer.cpp
    MeshRenderer.cpp ImGuiLayer.cpp)
add_library(emergences::core ALIAS emergences_core)
target_include_directories(emergences_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(emergences_core PRIVATE
    GLFW_INCLUDE_NONE
    $<$<CONFIG:Debug>:EMERGENCES_ENABLE_VALIDATION=1>)
target_link_libraries(emergences_core PUBLIC Vulkan::Vulkan glm::glm emergences::sim PRIVATE glfw emergences::imgui)
emergences_target_defaults(emergences_core)

if(BUILD_TESTING)
    add_executable(emergences_camera_tests Camera.cpp tests/CameraTests.cpp)
    target_include_directories(emergences_camera_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(emergences_camera_tests PRIVATE glm::glm)
    emergences_target_defaults(emergences_camera_tests)
    add_test(NAME camera_math COMMAND emergences_camera_tests)
endif()

if(BUILD_TESTING AND EMERGENCES_ENABLE_GPU_TESTS)
    add_executable(emergences_window_tests Window.cpp tests/WindowTests.cpp)
    target_include_directories(emergences_window_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(emergences_window_tests PRIVATE glfw emergences::imgui)
    emergences_target_defaults(emergences_window_tests)
    add_test(NAME window_fullscreen COMMAND emergences_window_tests)
    set_tests_properties(window_fullscreen PROPERTIES
        TIMEOUT 30 LABELS "gpu;window" RUN_SERIAL TRUE)
endif()
