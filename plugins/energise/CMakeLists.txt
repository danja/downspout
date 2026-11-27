add_library(downspout_energise_core STATIC
    src/energise_core.cpp
    src/energise_demo.cpp
    src/energise_quantise.cpp
    src/energise_serialization.cpp
    src/analysis/onsets.cpp
    src/analysis/tempo_grid.cpp
    src/analysis/classifier.cpp
    src/render/renderer.cpp
)
target_link_libraries(downspout_energise_core PUBLIC downspout-project-options)
target_include_directories(downspout_energise_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_energise_port INTERFACE)
target_link_libraries(downspout_energise_port INTERFACE
    downspout-project-options
    downspout_energise_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(energise
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/EnergisePlugin.cpp
        FILES_UI
            src/dpf/EnergiseUI.cpp
    )
    target_link_libraries(energise-dsp PUBLIC downspout_energise_core)
    target_link_libraries(energise-ui PUBLIC downspout-project-options)
    target_include_directories(energise PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(energise-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/energise.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_energise_core_tests
        SOURCES tests/energise_core_tests.cpp
        LIBS downspout_energise_core)
endif()
