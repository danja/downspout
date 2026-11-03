add_library(downspout_silken_core STATIC src/silken_core.cpp)
target_link_libraries(downspout_silken_core PUBLIC downspout-project-options)
target_include_directories(downspout_silken_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(silken
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SilkenPlugin.cpp
        FILES_UI
            src/dpf/SilkenUI.cpp
    )
    target_link_libraries(silken-dsp PUBLIC downspout_silken_core)
    target_link_libraries(silken-ui PUBLIC downspout-project-options downspout_silken_core)
    target_include_directories(silken PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(silken-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/silken.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_silken_core_tests
        SOURCES tests/silken_core_tests.cpp
        LIBS downspout_silken_core)
endif()
