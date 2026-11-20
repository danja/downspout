add_library(downspout_fontana_core STATIC
    src/fontana_engine.cpp
    src/fontana_sample_loader.cpp
    src/fontana_serialization.cpp
    src/fontana_slices.cpp
    src/fontana_soundfont.cpp
)
target_link_libraries(downspout_fontana_core PUBLIC downspout-project-options downspout_stb_vorbis)
target_include_directories(downspout_fontana_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_fontana_port INTERFACE)
target_link_libraries(downspout_fontana_port INTERFACE
    downspout-project-options
    downspout_fontana_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(fontana
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/FontanaPlugin.cpp
        FILES_UI
            src/dpf/FontanaUI.cpp
    )
    target_link_libraries(fontana-dsp PUBLIC downspout_fontana_core)
    target_link_libraries(fontana-ui PUBLIC downspout-project-options downspout_fontana_core)
    target_include_directories(fontana PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(fontana-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/third_party/stb_vorbis"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/fontana.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_fontana_core_tests
        SOURCES tests/fontana_core_tests.cpp
        LIBS downspout_fontana_core)
endif()
