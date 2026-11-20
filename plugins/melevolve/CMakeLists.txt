add_library(downspout_melevolve_core STATIC src/melevolve_core.cpp)
target_link_libraries(downspout_melevolve_core PUBLIC downspout-project-options)
target_include_directories(downspout_melevolve_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(melevolve
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MelevolvePlugin.cpp
        FILES_UI
            src/dpf/MelevolveUI.cpp
    )
    target_link_libraries(melevolve-dsp PUBLIC downspout_melevolve_core)
    target_link_libraries(melevolve-ui PUBLIC downspout_melevolve_core)
    target_include_directories(melevolve PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(melevolve-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/melevolve.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_melevolve_core_tests
        SOURCES tests/melevolve_core_tests.cpp
        LIBS downspout_melevolve_core)
endif()
