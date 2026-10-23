add_library(downspout_glue_core STATIC src/glue_core.cpp)
target_link_libraries(downspout_glue_core PUBLIC downspout-project-options)
target_include_directories(downspout_glue_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(glue
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/GluePlugin.cpp
        FILES_UI
            src/dpf/GlueUI.cpp
    )
    target_link_libraries(glue-dsp PUBLIC downspout_glue_core)
    target_link_libraries(glue-ui PUBLIC downspout-project-options downspout_glue_core)
    target_include_directories(glue PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(glue-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/glue.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_glue_core_tests
        SOURCES tests/glue_core_tests.cpp
        LIBS downspout_glue_core)
endif()
