add_library(downspout_snatch_core STATIC
    src/snatch_engine.cpp
    src/snatch_serialization.cpp
)
target_link_libraries(downspout_snatch_core PUBLIC downspout-project-options)
target_include_directories(downspout_snatch_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(snatch
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SnatchPlugin.cpp
        FILES_UI
            src/dpf/SnatchUI.cpp
    )
    target_link_libraries(snatch-dsp PUBLIC downspout_snatch_core)
    target_link_libraries(snatch-ui PUBLIC downspout-project-options downspout_snatch_core)
    target_include_directories(snatch PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(snatch-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/snatch.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_snatch_core_tests
        SOURCES tests/snatch_core_tests.cpp
        LIBS downspout_snatch_core)
endif()