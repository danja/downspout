add_library(downspout_moka_core STATIC
    src/moka_engine.cpp
)
target_link_libraries(downspout_moka_core PUBLIC downspout-project-options)
target_include_directories(downspout_moka_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(moka
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MokaPlugin.cpp
        FILES_UI
            src/dpf/MokaUI.cpp
    )
    target_link_libraries(moka-dsp PUBLIC downspout_moka_core)
    target_include_directories(moka PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/moka.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_moka_core_tests
        tests/moka_core_tests.cpp
    )
    target_link_libraries(downspout_moka_core_tests PRIVATE downspout_moka_core)
    add_test(NAME downspout_moka_core_tests COMMAND downspout_moka_core_tests)
endif()
