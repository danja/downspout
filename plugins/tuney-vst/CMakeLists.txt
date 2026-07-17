add_library(downspout_tuney_vst_core STATIC
    src/tuney_vst_engine.cpp
)
target_link_libraries(downspout_tuney_vst_core PUBLIC downspout-project-options)
target_include_directories(downspout_tuney_vst_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_tuney_vst_port INTERFACE)
target_link_libraries(downspout_tuney_vst_port INTERFACE
    downspout-project-options
    downspout_tuney_vst_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(tuney_vst
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/TuneyVstPlugin.cpp
        FILES_UI src/dpf/TuneyVstUI.cpp
    )
    target_link_libraries(tuney_vst-dsp PUBLIC downspout_tuney_vst_core)
    target_link_libraries(tuney_vst-ui PUBLIC downspout_tuney_vst_core)
    target_include_directories(tuney_vst PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(tuney_vst-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/tuney_vst.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_tuney_vst_core_tests tests/tuney_vst_core_tests.cpp)
    target_link_libraries(downspout_tuney_vst_core_tests PRIVATE downspout_tuney_vst_core)
    add_test(NAME downspout_tuney_vst_core_tests COMMAND downspout_tuney_vst_core_tests)
endif()
