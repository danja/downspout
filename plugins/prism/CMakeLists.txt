add_library(downspout_prism_core STATIC
    src/prism_engine.cpp
)
target_link_libraries(downspout_prism_core PUBLIC downspout-project-options)
target_include_directories(downspout_prism_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_prism_port INTERFACE)
target_link_libraries(downspout_prism_port INTERFACE
    downspout-project-options
    downspout_prism_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(prism
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/PrismPlugin.cpp
        FILES_UI
            src/dpf/PrismUI.cpp
    )
    target_link_libraries(prism-dsp PUBLIC downspout_prism_core)
    target_link_libraries(prism-ui PUBLIC downspout-project-options)
    target_include_directories(prism PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(prism-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/prism.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_prism_core_tests
        SOURCES tests/prism_core_tests.cpp
        LIBS downspout_prism_core)
endif()
