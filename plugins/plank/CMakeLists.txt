add_library(downspout_plank_core STATIC
    src/plank_core.cpp
    src/plank_voice.cpp
    src/plank_wavetables.cpp
)
target_link_libraries(downspout_plank_core PUBLIC downspout-project-options)
target_include_directories(downspout_plank_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_plank_port INTERFACE)
target_link_libraries(downspout_plank_port INTERFACE
    downspout-project-options
    downspout_plank_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(plank
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/PlankPlugin.cpp
        FILES_UI src/dpf/PlankUI.cpp
    )
    target_link_libraries(plank-dsp PUBLIC downspout_plank_core)
    target_link_libraries(plank-ui PUBLIC downspout_plank_core)
    target_include_directories(plank PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(plank-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/plank.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_plank_core_tests tests/plank_core_tests.cpp)
    target_link_libraries(downspout_plank_core_tests PRIVATE downspout_plank_core)
    add_test(NAME downspout_plank_core_tests COMMAND downspout_plank_core_tests)
endif()
