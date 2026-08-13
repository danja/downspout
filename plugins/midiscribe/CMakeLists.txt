add_library(downspout_midiscribe_core STATIC
    src/midiscribe_core.cpp
)
target_link_libraries(downspout_midiscribe_core PUBLIC downspout-project-options)
target_include_directories(downspout_midiscribe_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(midiscribe
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MidiscribePlugin.cpp
    )
    target_link_libraries(midiscribe-dsp PUBLIC downspout_midiscribe_core)
    target_include_directories(midiscribe PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/midiscribe.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_midiscribe_core_tests
        tests/midiscribe_core_tests.cpp
    )
    target_link_libraries(downspout_midiscribe_core_tests PRIVATE downspout_midiscribe_core)
    add_test(NAME downspout_midiscribe_core_tests COMMAND downspout_midiscribe_core_tests)
endif()
