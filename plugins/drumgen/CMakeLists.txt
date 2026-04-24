add_library(downspout_drumgen_core STATIC
    src/drumgen_engine.cpp
    src/drumgen_pattern.cpp
    src/drumgen_serialization.cpp
    src/drumgen_state.cpp
    src/drumgen_transport.cpp
    src/drumgen_variation.cpp
)
target_link_libraries(downspout_drumgen_core PUBLIC downspout-project-options)
target_include_directories(downspout_drumgen_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_drumgen_port INTERFACE)
target_link_libraries(downspout_drumgen_port INTERFACE
    downspout-project-options
    downspout_drumgen_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(drumgen
        TARGETS vst3
        FILES_DSP
            src/dpf/DrumgenPlugin.cpp
    )
    target_link_libraries(drumgen-dsp PUBLIC downspout_drumgen_core)
    target_include_directories(drumgen PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/drumgen.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_drumgen_core_tests
        tests/drumgen_core_tests.cpp
    )
    target_link_libraries(downspout_drumgen_core_tests PRIVATE downspout_drumgen_core)
    add_test(NAME downspout_drumgen_core_tests COMMAND downspout_drumgen_core_tests)
endif()
