add_library(downspout_ambo_core STATIC
    src/ambo_engine.cpp
    src/ambo_serialization.cpp
)
target_link_libraries(downspout_ambo_core PUBLIC downspout-project-options)
target_include_directories(downspout_ambo_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_ambo_port INTERFACE)
target_link_libraries(downspout_ambo_port INTERFACE
    downspout-project-options
    downspout_ambo_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(ambo
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/AmboPlugin.cpp
        FILES_UI
            src/dpf/AmboUI.cpp
    )
    target_link_libraries(ambo-dsp PUBLIC downspout_ambo_core)
    target_link_libraries(ambo-ui PUBLIC downspout-project-options)
    target_include_directories(ambo PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(ambo-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/ambo.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_ambo_core_tests
        tests/ambo_core_tests.cpp
    )
    target_link_libraries(downspout_ambo_core_tests PRIVATE downspout_ambo_core)
    add_test(NAME downspout_ambo_core_tests COMMAND downspout_ambo_core_tests)
endif()
