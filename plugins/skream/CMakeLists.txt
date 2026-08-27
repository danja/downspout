add_library(downspout_skream_core STATIC
    src/skream_engine.cpp
    src/skream_serialization.cpp
)
target_link_libraries(downspout_skream_core PUBLIC downspout-project-options)
target_include_directories(downspout_skream_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_skream_port INTERFACE)
target_link_libraries(downspout_skream_port INTERFACE
    downspout-project-options
    downspout_skream_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(skream
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SkreemPlugin.cpp
        FILES_UI
            src/dpf/SkreemUI.cpp
    )
    target_link_libraries(skream-dsp PUBLIC downspout_skream_core)
    target_link_libraries(skream-ui PUBLIC downspout-project-options)
    target_include_directories(skream PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(skream-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/skream.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_skream_core_tests
        tests/skream_core_tests.cpp
    )
    target_link_libraries(downspout_skream_core_tests PRIVATE downspout_skream_core)
    add_test(NAME downspout_skream_core_tests COMMAND downspout_skream_core_tests)
endif()
