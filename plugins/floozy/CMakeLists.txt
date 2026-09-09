add_library(downspout_floozy_core STATIC
    src/floozy_engine.cpp
)
target_link_libraries(downspout_floozy_core PUBLIC downspout-project-options)
target_include_directories(downspout_floozy_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/flues/pm"
)

add_library(downspout_floozy_port INTERFACE)
target_link_libraries(downspout_floozy_port INTERFACE
    downspout-project-options
    downspout_floozy_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(floozy
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/FloozyPlugin.cpp
        FILES_UI
            src/dpf/FloozyUI.cpp
    )
    target_link_libraries(floozy-dsp PUBLIC downspout_floozy_core)
    target_include_directories(floozy PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/flues/pm"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/floozy.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_floozy_core_tests
        tests/floozy_core_tests.cpp
    )
    target_link_libraries(downspout_floozy_core_tests PRIVATE downspout_floozy_core)
    add_test(NAME downspout_floozy_core_tests COMMAND downspout_floozy_core_tests)
endif()
