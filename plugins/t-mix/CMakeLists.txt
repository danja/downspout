add_library(downspout_t_mix_core STATIC
    src/t_mix_engine.cpp
    src/t_mix_serialization.cpp
)
target_link_libraries(downspout_t_mix_core PUBLIC downspout-project-options)
target_include_directories(downspout_t_mix_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_t_mix_port INTERFACE)
target_link_libraries(downspout_t_mix_port INTERFACE
    downspout-project-options
    downspout_t_mix_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(t_mix
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/TMixPlugin.cpp
        FILES_UI
            src/dpf/TMixUI.cpp
    )
    target_link_libraries(t_mix-dsp PUBLIC downspout_t_mix_core)
    target_link_libraries(t_mix-ui PUBLIC downspout-project-options)
    target_include_directories(t_mix PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(t_mix-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/t_mix.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_t_mix_core_tests
        tests/t_mix_core_tests.cpp
    )
    target_link_libraries(downspout_t_mix_core_tests PRIVATE downspout_t_mix_core)
    add_test(NAME downspout_t_mix_core_tests COMMAND downspout_t_mix_core_tests)
endif()
