add_library(downspout_sidecar_core STATIC
    src/sidecar_engine.cpp
    src/sidecar_protocol.cpp
    src/sidecar_serialization.cpp
)
target_link_libraries(downspout_sidecar_core PUBLIC downspout-project-options)
target_include_directories(downspout_sidecar_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_sidecar_port INTERFACE)
target_link_libraries(downspout_sidecar_port INTERFACE
    downspout-project-options
    downspout_sidecar_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(sidecar
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SidecarPlugin.cpp
        FILES_UI
            src/dpf/SidecarUI.cpp
    )
    target_link_libraries(sidecar-dsp PUBLIC downspout_sidecar_core)
    target_link_libraries(sidecar-ui PUBLIC downspout-project-options)
    target_include_directories(sidecar PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(sidecar-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/sidecar.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_sidecar_core_tests
        tests/sidecar_core_tests.cpp
    )
    target_link_libraries(downspout_sidecar_core_tests PRIVATE downspout_sidecar_core)
    add_test(NAME downspout_sidecar_core_tests COMMAND downspout_sidecar_core_tests)
endif()
