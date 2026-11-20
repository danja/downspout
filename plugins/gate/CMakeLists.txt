add_library(downspout_gate_core STATIC
    src/gate_engine.cpp
)
target_link_libraries(downspout_gate_core PUBLIC downspout-project-options)
target_include_directories(downspout_gate_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(gate
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/GatePlugin.cpp
        FILES_UI
            src/dpf/GateUI.cpp
    )
    target_link_libraries(gate-dsp PUBLIC downspout_gate_core)
    target_link_libraries(gate-ui PUBLIC downspout-project-options downspout_gate_core)
    target_include_directories(gate PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(gate-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/gate.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_gate_core_tests
        SOURCES tests/gate_core_tests.cpp
        LIBS downspout_gate_core
    )
endif()
