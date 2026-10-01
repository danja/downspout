add_library(downspout_keyframe_core STATIC
    src/keyframe_core.cpp
)
target_link_libraries(downspout_keyframe_core PUBLIC downspout-project-options)
target_include_directories(downspout_keyframe_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(keyframe
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/KeyframePlugin.cpp
        FILES_UI
            src/dpf/KeyframeUI.cpp
    )
    target_link_libraries(keyframe-dsp PUBLIC downspout_keyframe_core)
    target_link_libraries(keyframe-ui PUBLIC downspout-project-options)
    target_include_directories(keyframe PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(keyframe-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/keyframe.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_keyframe_core_tests
        tests/keyframe_core_tests.cpp
    )
    target_link_libraries(downspout_keyframe_core_tests PRIVATE downspout_keyframe_core)
    add_test(NAME downspout_keyframe_core_tests COMMAND downspout_keyframe_core_tests)
endif()