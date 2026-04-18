add_library(downspout_bassgen_core STATIC
    src/bassgen_engine.cpp
    src/bassgen_pattern.cpp
    src/bassgen_serialization.cpp
    src/bassgen_state.cpp
    src/bassgen_transport.cpp
    src/bassgen_variation.cpp
)
target_link_libraries(downspout_bassgen_core PUBLIC downspout-project-options)
target_include_directories(downspout_bassgen_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_bassgen_port INTERFACE)
target_link_libraries(downspout_bassgen_port INTERFACE
    downspout-project-options
    downspout_bassgen_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(bassgen
        TARGETS vst3
        FILES_DSP
            src/dpf/BassgenPlugin.cpp
        FILES_UI
            src/dpf/BassgenUI.cpp
    )
    target_link_libraries(bassgen-dsp PUBLIC downspout_bassgen_core)
    target_include_directories(bassgen PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/bassgen.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_bassgen_core_tests
        tests/bassgen_core_tests.cpp
    )
    target_link_libraries(downspout_bassgen_core_tests PRIVATE downspout_bassgen_core)
    add_test(NAME downspout_bassgen_core_tests COMMAND downspout_bassgen_core_tests)
endif()
