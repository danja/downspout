add_library(downspout_ghost_core STATIC src/ghost_core.cpp)
target_link_libraries(downspout_ghost_core PUBLIC downspout-project-options)
target_include_directories(downspout_ghost_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(ghost
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/GhostPlugin.cpp
        FILES_UI  src/dpf/GhostUI.cpp
    )
    target_link_libraries(ghost-dsp PUBLIC downspout_ghost_core)
    target_include_directories(ghost PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(ghost-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/ghost.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_ghost_core_tests
        tests/ghost_core_tests.cpp
    )
    target_link_libraries(downspout_ghost_core_tests PRIVATE downspout_ghost_core)
    target_include_directories(downspout_ghost_core_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
    )
    add_test(NAME downspout_ghost_core_tests COMMAND downspout_ghost_core_tests)
endif()
