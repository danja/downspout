add_library(downspout_cadence_core STATIC
    src/cadence_comping.cpp
    src/cadence_engine.cpp
    src/cadence_harmony.cpp
    src/cadence_serialization.cpp
    src/cadence_transport.cpp
    src/cadence_variation.cpp
)
target_link_libraries(downspout_cadence_core PUBLIC downspout-project-options)
target_include_directories(downspout_cadence_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_cadence_port INTERFACE)
target_link_libraries(downspout_cadence_port INTERFACE
    downspout-project-options
    downspout_cadence_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(cadence
        TARGETS vst3
        FILES_DSP
            src/dpf/CadencePlugin.cpp
        FILES_UI
            src/dpf/CadenceUI.cpp
    )
    target_link_libraries(cadence-dsp PUBLIC downspout_cadence_core)
    target_include_directories(cadence PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/cadence.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_cadence_core_tests
        tests/cadence_core_tests.cpp
    )
    target_link_libraries(downspout_cadence_core_tests PRIVATE downspout_cadence_core)
    add_test(NAME downspout_cadence_core_tests COMMAND downspout_cadence_core_tests)
endif()
