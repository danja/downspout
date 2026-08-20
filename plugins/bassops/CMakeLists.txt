add_library(downspout_bassops_core STATIC
    src/bassops_engine.cpp
    src/bassops_serialization.cpp
)
target_link_libraries(downspout_bassops_core PUBLIC downspout-project-options)
target_include_directories(downspout_bassops_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_bassops_port INTERFACE)
target_link_libraries(downspout_bassops_port INTERFACE
    downspout-project-options
    downspout_bassops_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(bassops
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/BassopsPlugin.cpp
        FILES_UI
            src/dpf/BassopsUI.cpp
    )
    target_link_libraries(bassops-dsp PUBLIC downspout_bassops_core)
    target_link_libraries(bassops-ui PUBLIC downspout-project-options)
    target_include_directories(bassops PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(bassops-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/bassops.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_bassops_core_tests
        tests/bassops_core_tests.cpp
    )
    target_link_libraries(downspout_bassops_core_tests PRIVATE downspout_bassops_core)
    add_test(NAME downspout_bassops_core_tests COMMAND downspout_bassops_core_tests)
endif()
