add_library(downspout_splay_core STATIC
    src/splay_engine.cpp
)
target_link_libraries(downspout_splay_core PUBLIC downspout-project-options)
target_include_directories(downspout_splay_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(splay
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SplayPlugin.cpp
        FILES_UI
            src/dpf/SplayUI.cpp
    )
    target_link_libraries(splay-dsp PUBLIC downspout_splay_core)
    target_link_libraries(splay-ui PUBLIC downspout-project-options downspout_splay_core)
    target_include_directories(splay PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(splay-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/splay.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_splay_core_tests
        SOURCES tests/splay_core_tests.cpp
        LIBS downspout_splay_core
    )
endif()
