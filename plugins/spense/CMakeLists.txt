add_library(downspout_spense_core STATIC src/spense_core.cpp)
target_link_libraries(downspout_spense_core PUBLIC downspout-project-options)
target_include_directories(downspout_spense_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(spense
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SpensePlugin.cpp
        FILES_UI
            src/dpf/SpenseUI.cpp
    )
    target_link_libraries(spense-dsp PUBLIC downspout_spense_core)
    target_link_libraries(spense-ui PUBLIC downspout-project-options downspout_spense_core)
    target_include_directories(spense PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(spense-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/spense.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_spense_core_tests
        SOURCES tests/spense_core_tests.cpp
        LIBS downspout_spense_core)
endif()
