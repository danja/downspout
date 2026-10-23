add_library(downspout_strata_core STATIC
    src/strata_tables.cpp
    src/strata_core.cpp
)
target_link_libraries(downspout_strata_core PUBLIC downspout-project-options)
target_include_directories(downspout_strata_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(strata
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/StrataPlugin.cpp
        FILES_UI
            src/dpf/StrataUI.cpp
    )
    target_link_libraries(strata-dsp PUBLIC downspout_strata_core)
    target_link_libraries(strata-ui PUBLIC downspout-project-options downspout_strata_core)
    target_include_directories(strata PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(strata-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/strata.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_strata_core_tests
        SOURCES tests/strata_core_tests.cpp
        LIBS downspout_strata_core)
endif()
