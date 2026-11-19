add_library(downspout_tilt_core STATIC
    src/tilt_engine.cpp
    src/tilt_spectrum.cpp
)
target_link_libraries(downspout_tilt_core PUBLIC downspout-project-options)
target_include_directories(downspout_tilt_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(tilt
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/TiltPlugin.cpp
        FILES_UI
            src/dpf/TiltUI.cpp
    )
    target_link_libraries(tilt-dsp PUBLIC downspout_tilt_core)
    target_link_libraries(tilt-ui PUBLIC downspout-project-options downspout_tilt_core)
    target_include_directories(tilt PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(tilt-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/tilt.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_tilt_core_tests
        SOURCES tests/tilt_core_tests.cpp
        LIBS downspout_tilt_core
    )
endif()
