add_library(downspout_beards_core STATIC
    src/beards_engine.cpp
)
target_link_libraries(downspout_beards_core PUBLIC downspout-project-options)
target_include_directories(downspout_beards_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_beards_port INTERFACE)
target_link_libraries(downspout_beards_port INTERFACE
    downspout-project-options
    downspout_beards_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(beards
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/BeardsPlugin.cpp
        FILES_UI
            src/dpf/BeardsUI.cpp
    )
    target_link_libraries(beards-dsp PUBLIC downspout_beards_core)
    target_link_libraries(beards-ui PUBLIC downspout-project-options)
    target_include_directories(beards PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(beards-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/beards.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_beards_core_tests
        SOURCES tests/beards_core_tests.cpp
        LIBS downspout_beards_core)
endif()
