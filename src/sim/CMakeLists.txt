add_library(emergences_sim INTERFACE)
add_library(emergences::sim ALIAS emergences_sim)
target_compile_features(emergences_sim INTERFACE cxx_std_20)
target_include_directories(emergences_sim INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(emergences_sim INTERFACE glm::glm)

if(BUILD_TESTING)
    # Compile every public header independently. This also checks Agent's GPU ABI
    # and proves the simulation layer builds without Vulkan or GLFW installed.
    set(header_checks)
    foreach(header Agent Backend World Rule Intervention Simulation)
        set(check "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${header}.cpp")
        file(GENERATE OUTPUT "${check}" CONTENT "#include \"sim/${header}.h\"\n")
        list(APPEND header_checks "${check}")
    endforeach()
    add_library(emergences_sim_header_check OBJECT ${header_checks})
    target_link_libraries(emergences_sim_header_check PRIVATE emergences::sim)
    emergences_target_defaults(emergences_sim_header_check)
endif()
