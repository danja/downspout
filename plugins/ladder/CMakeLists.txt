add_library(downspout_ladder_core STATIC src/ladder_core.cpp)
target_link_libraries(downspout_ladder_core PUBLIC downspout-project-options)
target_include_directories(downspout_ladder_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(ladder
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/LadderPlugin.cpp
        FILES_UI
            src/dpf/LadderUI.cpp
    )
    target_link_libraries(ladder-dsp PUBLIC downspout_ladder_core)
    target_link_libraries(ladder-ui PUBLIC downspout-project-options downspout_ladder_core)
    target_include_directories(ladder PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(ladder-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/ladder.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_ladder_core_tests
        tests/ladder_core_tests.cpp
    )
    target_link_libraries(downspout_ladder_core_tests PRIVATE downspout_ladder_core)
    add_test(NAME downspout_ladder_core_tests COMMAND downspout_ladder_core_tests)
endif()
