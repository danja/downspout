add_library(downspout_campione_core STATIC
    src/campione_engine.cpp
    src/campione_serialization.cpp
    src/campione_sample_loader.cpp
    src/campione_pitch_utils.cpp
)
target_link_libraries(downspout_campione_core PUBLIC downspout-project-options)
target_include_directories(downspout_campione_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

add_library(downspout_campione_port INTERFACE)
target_link_libraries(downspout_campione_port INTERFACE
    downspout-project-options
    downspout_campione_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(campione
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP
            src/dpf/CampionePlugin.cpp
        FILES_UI
            src/dpf/CampioneUI.cpp
    )
    target_link_libraries(campione-dsp PUBLIC downspout_campione_core)
    target_link_libraries(campione-ui PUBLIC downspout-project-options downspout_campione_core)
    target_include_directories(campione PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(campione-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/campione.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_campione_core_tests
        tests/campione_core_tests.cpp
    )
    target_link_libraries(downspout_campione_core_tests PRIVATE downspout_campione_core)
    add_test(NAME downspout_campione_core_tests COMMAND downspout_campione_core_tests)
endif()
