add_library(downspout_flues_synth_driver_core STATIC
    src/flues_synth_driver_processor.cpp
)
target_link_libraries(downspout_flues_synth_driver_core PUBLIC downspout-project-options)
target_include_directories(downspout_flues_synth_driver_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(flues_synth_driver
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/FlueSynthDriverPlugin.cpp
        FILES_UI
            src/dpf/FlueSynthDriverUI.cpp
    )
    target_link_libraries(flues_synth_driver-dsp PUBLIC downspout_flues_synth_driver_core)
    target_include_directories(flues_synth_driver PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/flues_synth_driver.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_flues_synth_driver_core_tests
        tests/flues_synth_driver_core_tests.cpp
    )
    target_link_libraries(downspout_flues_synth_driver_core_tests
        PRIVATE downspout_flues_synth_driver_core)
    add_test(NAME downspout_flues_synth_driver_core_tests
             COMMAND downspout_flues_synth_driver_core_tests)
endif()
