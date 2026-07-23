add_library(downspout_xoxolo_core STATIC
    src/xoxolo_engine.cpp
    src/xoxolo_generator.cpp
    src/xoxolo_serialization.cpp
)
target_link_libraries(downspout_xoxolo_core PUBLIC downspout-project-options)
target_include_directories(downspout_xoxolo_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_xoxolo_port INTERFACE)
target_link_libraries(downspout_xoxolo_port INTERFACE
    downspout-project-options
    downspout_xoxolo_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(xoxolo
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/XoxoloPlugin.cpp
        FILES_UI
            src/dpf/XoxoloUI.cpp
    )
    target_link_libraries(xoxolo-dsp PUBLIC downspout_xoxolo_core)
    target_link_libraries(xoxolo-ui PUBLIC downspout_xoxolo_core)
    target_include_directories(xoxolo PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(xoxolo-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/xoxolo.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_xoxolo_core_tests
        tests/xoxolo_core_tests.cpp
    )
    target_link_libraries(downspout_xoxolo_core_tests PRIVATE downspout_xoxolo_core)
    add_test(NAME downspout_xoxolo_core_tests COMMAND downspout_xoxolo_core_tests)
endif()
