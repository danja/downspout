add_library(downspout_lifeform_core STATIC
    src/lifeform_processor.cpp
)
target_link_libraries(downspout_lifeform_core PUBLIC downspout-project-options)
target_include_directories(downspout_lifeform_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_lifeform_port INTERFACE)
target_link_libraries(downspout_lifeform_port INTERFACE
    downspout-project-options
    downspout_lifeform_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(lifeform
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/LifeformPlugin.cpp
        FILES_UI
            src/dpf/LifeformUI.cpp
    )
    target_link_libraries(lifeform-dsp PUBLIC downspout_lifeform_core)
    target_include_directories(lifeform PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/lifeform.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_lifeform_core_tests
        tests/lifeform_core_tests.cpp
    )
    target_link_libraries(downspout_lifeform_core_tests PRIVATE downspout_lifeform_core)
    add_test(NAME downspout_lifeform_core_tests COMMAND downspout_lifeform_core_tests)
endif()
