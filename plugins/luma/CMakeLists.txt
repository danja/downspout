add_library(downspout_luma_core STATIC
    src/luma_processor.cpp
)
target_link_libraries(downspout_luma_core PUBLIC downspout-project-options)
target_include_directories(downspout_luma_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_luma_port INTERFACE)
target_link_libraries(downspout_luma_port INTERFACE
    downspout-project-options
    downspout_luma_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(luma
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/LumaPlugin.cpp
        FILES_UI
            src/dpf/LumaUI.cpp
    )
    target_link_libraries(luma-dsp PUBLIC downspout_luma_core)
    target_include_directories(luma PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/luma.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_luma_core_tests
        tests/luma_core_tests.cpp
    )
    target_link_libraries(downspout_luma_core_tests PRIVATE downspout_luma_core)
    add_test(NAME downspout_luma_core_tests COMMAND downspout_luma_core_tests)
endif()
