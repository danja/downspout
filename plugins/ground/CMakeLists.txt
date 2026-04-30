add_library(downspout_ground_core STATIC
    src/ground_engine.cpp
    src/ground_pattern.cpp
    src/ground_serialization.cpp
    src/ground_variation.cpp
)
target_link_libraries(downspout_ground_core PUBLIC downspout-project-options)
target_include_directories(downspout_ground_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_ground_port INTERFACE)
target_link_libraries(downspout_ground_port INTERFACE
    downspout-project-options
    downspout_ground_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(ground
        TARGETS vst3
        FILES_DSP
            src/dpf/GroundPlugin.cpp
        FILES_UI
            src/dpf/GroundUI.cpp
    )
    target_link_libraries(ground-dsp PUBLIC downspout_ground_core)
    target_include_directories(ground PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/ground.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_ground_core_tests
        tests/ground_core_tests.cpp
    )
    target_link_libraries(downspout_ground_core_tests PRIVATE downspout_ground_core)
    add_test(NAME downspout_ground_core_tests COMMAND downspout_ground_core_tests)
endif()
