add_library(downspout_midiscribe_core STATIC
    src/midiscribe_core.cpp
)
target_link_libraries(downspout_midiscribe_core PUBLIC downspout-project-options)
target_include_directories(downspout_midiscribe_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(midiscribe
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP
            src/dpf/MidiscribePlugin.cpp
        FILES_UI
            src/dpf/MidiscribeUI.cpp
    )
    target_link_libraries(midiscribe-dsp PUBLIC downspout_midiscribe_core)
    target_link_libraries(midiscribe-ui PUBLIC downspout-project-options)
    target_include_directories(midiscribe PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(midiscribe-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/midiscribe.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_midiscribe_core_tests
        SOURCES tests/midiscribe_core_tests.cpp
        LIBS downspout_midiscribe_core)
endif()
