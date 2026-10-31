add_library(downspout_interleaver_core STATIC src/interleaver_engine.cpp)
target_link_libraries(downspout_interleaver_core PUBLIC downspout-project-options)
target_include_directories(downspout_interleaver_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(interleaver
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/InterleaverPlugin.cpp
        FILES_UI
            src/dpf/InterleaverUI.cpp
    )
    target_link_libraries(interleaver-dsp PUBLIC downspout_interleaver_core)
    target_link_libraries(interleaver-ui PUBLIC downspout-project-options downspout_interleaver_core)
    target_include_directories(interleaver PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(interleaver-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/interleaver.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_interleaver_core_tests
        SOURCES tests/interleaver_core_tests.cpp
        LIBS downspout_interleaver_core)
endif()
