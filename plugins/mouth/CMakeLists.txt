add_library(downspout_mouth_core STATIC
    src/mouth_engine.cpp
)
target_link_libraries(downspout_mouth_core PUBLIC downspout-project-options)
target_include_directories(downspout_mouth_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_mouth_port INTERFACE)
target_link_libraries(downspout_mouth_port INTERFACE
    downspout-project-options
    downspout_mouth_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(mouth
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MouthPlugin.cpp
        FILES_UI
            src/dpf/MouthUI.cpp
    )
    target_link_libraries(mouth-dsp PUBLIC downspout_mouth_core)
    target_link_libraries(mouth-ui PUBLIC downspout-project-options)
    target_include_directories(mouth PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(mouth-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/mouth.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_mouth_core_tests
        SOURCES tests/mouth_core_tests.cpp
        LIBS downspout_mouth_core)
endif()
