add_library(downspout_polyscribe_core STATIC
    src/polyscribe_engine.cpp
)
target_link_libraries(downspout_polyscribe_core PUBLIC downspout-project-options)
target_include_directories(downspout_polyscribe_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(polyscribe
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/PolyscribePlugin.cpp
        FILES_UI
            src/dpf/PolyscribeUI.cpp
    )
    target_link_libraries(polyscribe-dsp PUBLIC downspout_polyscribe_core)
    target_link_libraries(polyscribe-ui PUBLIC downspout-project-options downspout_polyscribe_core)
    target_include_directories(polyscribe PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(polyscribe-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/polyscribe.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_polyscribe_core_tests
        SOURCES tests/polyscribe_core_tests.cpp
        LIBS downspout_polyscribe_core
    )
endif()
