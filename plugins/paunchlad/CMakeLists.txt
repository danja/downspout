add_library(downspout_paunchlad_core STATIC
    src/paunchlad_processor.cpp
)
target_link_libraries(downspout_paunchlad_core PUBLIC downspout-project-options)
target_include_directories(downspout_paunchlad_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_paunchlad_port INTERFACE)
target_link_libraries(downspout_paunchlad_port INTERFACE
    downspout-project-options
    downspout_paunchlad_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(paunchlad
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/PaunchLadPlugin.cpp
        FILES_UI
            src/dpf/PaunchLadUI.cpp
    )
    target_link_libraries(paunchlad-dsp PUBLIC downspout_paunchlad_core)
    target_include_directories(paunchlad PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/paunchlad.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_paunchlad_core_tests
        tests/paunchlad_core_tests.cpp
    )
    target_link_libraries(downspout_paunchlad_core_tests PRIVATE downspout_paunchlad_core)
    add_test(NAME downspout_paunchlad_core_tests COMMAND downspout_paunchlad_core_tests)
endif()
