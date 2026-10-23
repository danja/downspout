add_library(downspout_retune_core STATIC
    src/retune_scale.cpp
    src/retune_engine.cpp
)
target_link_libraries(downspout_retune_core PUBLIC downspout-project-options)
target_include_directories(downspout_retune_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(retune
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP
            src/dpf/RetunePlugin.cpp
        FILES_UI
            src/dpf/RetuneUI.cpp
    )
    target_link_libraries(retune-dsp PUBLIC downspout_retune_core)
    target_link_libraries(retune-ui PUBLIC downspout-project-options downspout_retune_core)
    target_include_directories(retune PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(retune-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/retune.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_retune_core_tests
        SOURCES tests/retune_core_tests.cpp
        LIBS downspout_retune_core
        DEFINITIONS RETUNE_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scales")
endif()
