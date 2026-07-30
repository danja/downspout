add_library(downspout_canticle_core STATIC
    src/canticle_engine.cpp
)
target_link_libraries(downspout_canticle_core PUBLIC downspout-project-options)
target_include_directories(downspout_canticle_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_canticle_port INTERFACE)
target_link_libraries(downspout_canticle_port INTERFACE
    downspout-project-options
    downspout_canticle_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(canticle
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/CanticlePlugin.cpp
        FILES_UI
            src/dpf/CanticleUI.cpp
    )
    target_link_libraries(canticle-dsp PUBLIC downspout_canticle_core)
    target_include_directories(canticle PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/canticle.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_canticle_core_tests
        tests/canticle_core_tests.cpp
    )
    target_link_libraries(downspout_canticle_core_tests PRIVATE downspout_canticle_core)
    add_test(NAME downspout_canticle_core_tests COMMAND downspout_canticle_core_tests)

    add_executable(downspout_canticle_processing_benchmark
        tests/canticle_processing_benchmark.cpp
    )
    target_link_libraries(downspout_canticle_processing_benchmark PRIVATE downspout_canticle_core)
endif()
