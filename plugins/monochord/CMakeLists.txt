add_library(downspout_monochord_core STATIC src/monochord_core.cpp)
target_link_libraries(downspout_monochord_core PUBLIC downspout-project-options)
target_include_directories(downspout_monochord_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(monochord
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MonochordPlugin.cpp
        FILES_UI
            src/dpf/MonochordUI.cpp
    )
    target_link_libraries(monochord-dsp PUBLIC downspout_monochord_core)
    target_link_libraries(monochord-ui PUBLIC downspout-project-options downspout_monochord_core)
    target_include_directories(monochord PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(monochord-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/monochord.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_monochord_core_tests
        SOURCES tests/monochord_core_tests.cpp
        LIBS downspout_monochord_core)
endif()
