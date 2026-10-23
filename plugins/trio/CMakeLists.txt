add_library(downspout_trio_core STATIC src/trio_core.cpp)
target_link_libraries(downspout_trio_core PUBLIC downspout-project-options)
target_include_directories(downspout_trio_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(trio
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/TrioPlugin.cpp
        FILES_UI
            src/dpf/TrioUI.cpp
    )
    target_link_libraries(trio-dsp PUBLIC downspout_trio_core)
    target_link_libraries(trio-ui PUBLIC downspout-project-options downspout_trio_core)
    target_include_directories(trio PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(trio-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/trio.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_trio_core_tests
        tests/trio_core_tests.cpp
    )
    target_link_libraries(downspout_trio_core_tests PRIVATE downspout_trio_core)
    add_test(NAME downspout_trio_core_tests COMMAND downspout_trio_core_tests)
endif()
