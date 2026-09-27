add_library(downspout_quefrency_core STATIC
    src/quefrency_core.cpp
)
target_link_libraries(downspout_quefrency_core PUBLIC downspout-project-options)
target_include_directories(downspout_quefrency_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(quefrency
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/QuefrencyPlugin.cpp
        FILES_UI
            src/dpf/QuefrencyUI.cpp
    )
    target_link_libraries(quefrency-dsp PUBLIC downspout_quefrency_core)
    target_link_libraries(quefrency-ui PUBLIC downspout-project-options)
    target_include_directories(quefrency PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(quefrency-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/quefrency.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_quefrency_core_tests
        tests/quefrency_core_tests.cpp
    )
    target_link_libraries(downspout_quefrency_core_tests PRIVATE downspout_quefrency_core)
    add_test(NAME downspout_quefrency_core_tests COMMAND downspout_quefrency_core_tests)
endif()
