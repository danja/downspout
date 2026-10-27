add_library(downspout_thumper_core STATIC
    src/thumper_engine.cpp
    src/thumper_serialization.cpp
)
target_link_libraries(downspout_thumper_core PUBLIC downspout-project-options)
target_include_directories(downspout_thumper_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(thumper
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/ThumperPlugin.cpp
        FILES_UI
            src/dpf/ThumperUI.cpp
    )
    target_link_libraries(thumper-dsp PUBLIC downspout_thumper_core)
    target_link_libraries(thumper-ui PUBLIC downspout-project-options)
    target_include_directories(thumper PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(thumper-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/thumper.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_thumper_core_tests
        SOURCES tests/thumper_core_tests.cpp
        LIBS downspout_thumper_core)
endif()