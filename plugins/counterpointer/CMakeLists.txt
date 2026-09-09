add_library(downspout_counterpointer_core STATIC
    src/counterpointer_engine.cpp
    src/counterpointer_serialization.cpp
    src/counterpointer_transport.cpp
)
target_link_libraries(downspout_counterpointer_core PUBLIC downspout-project-options)
target_include_directories(downspout_counterpointer_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_counterpointer_port INTERFACE)
target_link_libraries(downspout_counterpointer_port INTERFACE
    downspout-project-options
    downspout_counterpointer_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(counterpointer
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/CounterpointerPlugin.cpp
        FILES_UI
            src/dpf/CounterpointerUI.cpp
    )
    target_link_libraries(counterpointer-dsp PUBLIC downspout_counterpointer_core)
    target_link_libraries(counterpointer-ui PUBLIC downspout-project-options)
    target_include_directories(counterpointer PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(counterpointer-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/counterpointer.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_counterpointer_core_tests
        tests/counterpointer_core_tests.cpp
    )
    target_link_libraries(downspout_counterpointer_core_tests PRIVATE downspout_counterpointer_core)
    add_test(NAME downspout_counterpointer_core_tests COMMAND downspout_counterpointer_core_tests)
endif()
