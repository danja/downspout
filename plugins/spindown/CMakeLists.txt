add_library(downspout_spindown_core STATIC src/spindown_core.cpp)
target_link_libraries(downspout_spindown_core PUBLIC downspout-project-options)
target_include_directories(downspout_spindown_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(spindown
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SpindownPlugin.cpp
        FILES_UI
            src/dpf/SpindownUI.cpp
    )
    target_link_libraries(spindown-dsp PUBLIC downspout_spindown_core)
    target_link_libraries(spindown-ui PUBLIC downspout-project-options downspout_spindown_core)
    target_include_directories(spindown PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(spindown-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/spindown.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_spindown_core_tests
        SOURCES tests/spindown_core_tests.cpp
        LIBS downspout_spindown_core)
endif()
