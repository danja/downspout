add_library(downspout_worms_core STATIC
    src/worms_engine.cpp
    src/worms_pattern.cpp
    src/worms_serialization.cpp
    src/worms_tonnetz.cpp
)
target_link_libraries(downspout_worms_core PUBLIC downspout-project-options)
target_include_directories(downspout_worms_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/plugins/generative-common/include"
)

add_library(downspout_worms_port INTERFACE)
target_link_libraries(downspout_worms_port INTERFACE
    downspout-project-options
    downspout_worms_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(worms
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/WormsPlugin.cpp
        FILES_UI
            src/dpf/WormsUI.cpp
    )
    target_link_libraries(worms-dsp PUBLIC downspout_worms_core)
    target_include_directories(worms PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${CMAKE_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/worms.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_worms_core_tests
        SOURCES tests/worms_core_tests.cpp
        LIBS downspout_worms_core
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
