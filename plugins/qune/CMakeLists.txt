add_library(downspout_qune_core STATIC
    src/qune_pitch.cpp
    src/qune_fast.cpp
    src/qune_psola.cpp
    src/qune_spectral.cpp
    src/qune_engine.cpp
)
target_link_libraries(downspout_qune_core PUBLIC downspout-project-options)
target_include_directories(downspout_qune_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_qune_port INTERFACE)
target_link_libraries(downspout_qune_port INTERFACE
    downspout-project-options
    downspout_qune_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(qune
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/QunePlugin.cpp
        FILES_UI
            src/dpf/QuneUI.cpp
    )
    target_link_libraries(qune-dsp PUBLIC downspout_qune_core)
    target_link_libraries(qune-ui PUBLIC downspout-project-options)
    target_include_directories(qune PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(qune-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/qune.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_qune_core_tests
        SOURCES tests/qune_core_tests.cpp
        LIBS downspout_qune_core)
endif()
