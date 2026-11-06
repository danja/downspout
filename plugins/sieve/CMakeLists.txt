add_library(downspout_sieve_core STATIC
    src/sieve_engine.cpp
)
target_link_libraries(downspout_sieve_core PUBLIC downspout-project-options)
target_include_directories(downspout_sieve_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_sieve_port INTERFACE)
target_link_libraries(downspout_sieve_port INTERFACE
    downspout-project-options
    downspout_sieve_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(sieve
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SievePlugin.cpp
        FILES_UI
            src/dpf/SieveUI.cpp
    )
    target_link_libraries(sieve-dsp PUBLIC downspout_sieve_core)
    target_link_libraries(sieve-ui PUBLIC downspout-project-options)
    target_include_directories(sieve PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(sieve-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/sieve.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_sieve_core_tests
        SOURCES tests/sieve_core_tests.cpp
        LIBS downspout_sieve_core)
endif()