add_library(downspout_basilico_core STATIC
    src/basilico_engine.cpp
    src/basilico_flanger.cpp
    src/basilico_modulation.cpp
)
target_link_libraries(downspout_basilico_core PUBLIC downspout-project-options)
target_include_directories(downspout_basilico_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_basilico_port INTERFACE)
target_link_libraries(downspout_basilico_port INTERFACE
    downspout-project-options
    downspout_basilico_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(basilico
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/BasilicoPlugin.cpp
        FILES_UI
            src/dpf/BasilicoUI.cpp
    )
    target_link_libraries(basilico-dsp PUBLIC downspout_basilico_core)
    target_include_directories(basilico PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/basilico.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_basilico_core_tests
        tests/basilico_core_tests.cpp
    )
    target_link_libraries(downspout_basilico_core_tests PRIVATE downspout_basilico_core)
    add_test(NAME downspout_basilico_core_tests COMMAND downspout_basilico_core_tests)
endif()
