add_library(downspout_bubbles_core STATIC
    src/bubbles_engine.cpp
    src/bubbles_serialization.cpp
)
target_link_libraries(downspout_bubbles_core PUBLIC downspout-project-options)
target_include_directories(downspout_bubbles_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_bubbles_port INTERFACE)
target_link_libraries(downspout_bubbles_port INTERFACE
    downspout-project-options
    downspout_bubbles_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(bubbles
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/BubblesPlugin.cpp
        FILES_UI
            src/dpf/BubblesUI.cpp
    )
    target_link_libraries(bubbles-dsp PUBLIC downspout_bubbles_core)
    target_link_libraries(bubbles-ui  PUBLIC downspout-project-options)
    target_include_directories(bubbles PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(bubbles-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/bubbles.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_bubbles_core_tests
        tests/bubbles_core_tests.cpp
    )
    target_link_libraries(downspout_bubbles_core_tests PRIVATE downspout_bubbles_core)
    add_test(NAME downspout_bubbles_core_tests COMMAND downspout_bubbles_core_tests)
endif()
