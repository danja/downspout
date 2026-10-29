add_library(downspout_heavyverb_core STATIC src/heavyverb_core.cpp)
target_link_libraries(downspout_heavyverb_core PUBLIC downspout-project-options)
target_include_directories(downspout_heavyverb_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(heavyverb
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/HeavyVerbPlugin.cpp
        FILES_UI
            src/dpf/HeavyVerbUI.cpp
    )
    target_link_libraries(heavyverb-dsp PUBLIC downspout_heavyverb_core)
    target_link_libraries(heavyverb-ui PUBLIC downspout-project-options downspout_heavyverb_core)
    target_include_directories(heavyverb PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(heavyverb-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/heavyverb.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_heavyverb_core_tests
        SOURCES tests/heavyverb_core_tests.cpp
        LIBS downspout_heavyverb_core)
endif()
