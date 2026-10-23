add_library(downspout_quartet_core STATIC
    src/quartet_core.cpp
)
target_link_libraries(downspout_quartet_core PUBLIC downspout-project-options)
target_include_directories(downspout_quartet_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(quartet
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/QuartetPlugin.cpp
        FILES_UI
            src/dpf/QuartetUI.cpp
    )
    target_link_libraries(quartet-dsp PUBLIC downspout_quartet_core)
    target_link_libraries(quartet-ui PUBLIC downspout-project-options downspout_quartet_core)
    target_include_directories(quartet PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(quartet-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/quartet.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_quartet_core_tests
        tests/quartet_core_tests.cpp
    )
    target_link_libraries(downspout_quartet_core_tests PRIVATE downspout_quartet_core)
    add_test(NAME downspout_quartet_core_tests COMMAND downspout_quartet_core_tests)
endif()
