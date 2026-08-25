add_library(downspout_damiano_core STATIC
    src/damiano_engine.cpp
    src/damiano_serialization.cpp
)
target_link_libraries(downspout_damiano_core PUBLIC downspout-project-options)
target_include_directories(downspout_damiano_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_damiano_port INTERFACE)
target_link_libraries(downspout_damiano_port INTERFACE
    downspout-project-options
    downspout_damiano_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(damiano
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/DamianoPlugin.cpp
        FILES_UI
            src/dpf/DamianoUI.cpp
    )
    target_link_libraries(damiano-dsp PUBLIC downspout_damiano_core)
    target_link_libraries(damiano-ui PUBLIC downspout-project-options)
    target_include_directories(damiano PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(damiano-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/damiano.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_damiano_core_tests
        tests/damiano_core_tests.cpp
    )
    target_link_libraries(downspout_damiano_core_tests PRIVATE downspout_damiano_core)
    add_test(NAME downspout_damiano_core_tests COMMAND downspout_damiano_core_tests)
endif()
