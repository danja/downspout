add_library(downspout_syrinx_core STATIC
    src/syrinx_engine.cpp
)
target_link_libraries(downspout_syrinx_core PUBLIC downspout-project-options)
target_include_directories(downspout_syrinx_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(syrinx
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SyrinxPlugin.cpp
        FILES_UI
            src/dpf/SyrinxUI.cpp
    )
    target_link_libraries(syrinx-dsp PUBLIC downspout_syrinx_core)
    target_include_directories(syrinx PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/syrinx.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_syrinx_core_tests
        tests/syrinx_core_tests.cpp
    )
    target_link_libraries(downspout_syrinx_core_tests PRIVATE downspout_syrinx_core)
    add_test(NAME downspout_syrinx_core_tests COMMAND downspout_syrinx_core_tests)
endif()
