add_library(downspout_chipper_core STATIC src/chipper_core.cpp)
target_link_libraries(downspout_chipper_core PUBLIC downspout-project-options)
target_include_directories(downspout_chipper_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(chipper
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/ChipperPlugin.cpp
        FILES_UI  src/dpf/ChipperUI.cpp
    )
    target_link_libraries(chipper-dsp PUBLIC downspout_chipper_core)
    target_include_directories(chipper PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(chipper-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/chipper.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_chipper_core_tests
        tests/chipper_core_tests.cpp
    )
    target_link_libraries(downspout_chipper_core_tests PRIVATE downspout_chipper_core)
    add_test(NAME downspout_chipper_core_tests COMMAND downspout_chipper_core_tests)
endif()
