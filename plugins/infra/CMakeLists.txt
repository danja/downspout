add_library(downspout_infra_core STATIC src/infra_core.cpp)
target_link_libraries(downspout_infra_core PUBLIC downspout-project-options)
target_include_directories(downspout_infra_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(infra
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/InfraPlugin.cpp
        FILES_UI
            src/dpf/InfraUI.cpp
    )
    target_link_libraries(infra-dsp PUBLIC downspout_infra_core)
    target_link_libraries(infra-ui PUBLIC downspout-project-options downspout_infra_core)
    target_include_directories(infra PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(infra-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/infra.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_infra_core_tests
        SOURCES tests/infra_core_tests.cpp
        LIBS downspout_infra_core)
endif()
