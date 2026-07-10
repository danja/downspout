add_library(downspout_arpgen_core STATIC
    src/arpgen_core.cpp
)
target_link_libraries(downspout_arpgen_core PUBLIC downspout-project-options)
target_include_directories(downspout_arpgen_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_arpgen_port INTERFACE)
target_link_libraries(downspout_arpgen_port INTERFACE
    downspout-project-options
    downspout_arpgen_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(arpgen
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/ArpgenPlugin.cpp
        FILES_UI
            src/dpf/ArpgenUI.cpp
    )
    target_link_libraries(arpgen-dsp PUBLIC downspout_arpgen_core)
    target_link_libraries(arpgen-ui PUBLIC downspout-project-options)
    target_include_directories(arpgen PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(arpgen-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/arpgen.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_arpgen_core_tests
        tests/arpgen_core_tests.cpp
    )
    target_link_libraries(downspout_arpgen_core_tests PRIVATE downspout_arpgen_core)
    if(MSVC)
        target_compile_options(downspout_arpgen_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_arpgen_core_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME downspout_arpgen_core_tests COMMAND downspout_arpgen_core_tests)
endif()
