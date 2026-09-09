add_library(downspout_melgen_core STATIC
    src/melgen_engine.cpp
    src/melgen_pattern.cpp
    src/melgen_serialization.cpp
    src/melgen_state.cpp
    src/melgen_transport.cpp
    src/melgen_variation.cpp
)
target_link_libraries(downspout_melgen_core PUBLIC downspout-project-options)
target_include_directories(downspout_melgen_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_melgen_port INTERFACE)
target_link_libraries(downspout_melgen_port INTERFACE
    downspout-project-options
    downspout_melgen_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(melgen
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MelgenPlugin.cpp
        FILES_UI
            src/dpf/MelgenUI.cpp
    )
    target_link_libraries(melgen-dsp PUBLIC downspout_melgen_core)
    target_include_directories(melgen PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/melgen.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_melgen_core_tests
        tests/melgen_core_tests.cpp
    )
    target_link_libraries(downspout_melgen_core_tests PRIVATE downspout_melgen_core)
    add_test(NAME downspout_melgen_core_tests COMMAND downspout_melgen_core_tests)
endif()
