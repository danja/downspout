add_library(downspout_grist_core STATIC src/grist_core.cpp)
target_link_libraries(downspout_grist_core PUBLIC downspout-project-options)
target_include_directories(downspout_grist_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(grist
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/GristPlugin.cpp
        FILES_UI
            src/dpf/GristUI.cpp
    )
    target_link_libraries(grist-dsp PUBLIC downspout_grist_core)
    target_link_libraries(grist-ui PUBLIC downspout_grist_core)
    target_include_directories(grist PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(grist-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/grist.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_grist_core_tests
        SOURCES tests/grist_core_tests.cpp
        LIBS downspout_grist_core)
endif()
