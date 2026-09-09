add_library(downspout_m_mix_core STATIC
    src/m_mix_engine.cpp
    src/m_mix_serialization.cpp
)
target_link_libraries(downspout_m_mix_core PUBLIC downspout-project-options)
target_include_directories(downspout_m_mix_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_m_mix_port INTERFACE)
target_link_libraries(downspout_m_mix_port INTERFACE
    downspout-project-options
    downspout_m_mix_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(m_mix
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MMixPlugin.cpp
        FILES_UI
            src/dpf/MMixUI.cpp
    )
    target_link_libraries(m_mix-dsp PUBLIC downspout_m_mix_core)
    target_link_libraries(m_mix-ui PUBLIC downspout-project-options)
    target_include_directories(m_mix PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(m_mix-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/m_mix.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_m_mix_core_tests
        tests/m_mix_core_tests.cpp
    )
    target_link_libraries(downspout_m_mix_core_tests PRIVATE downspout_m_mix_core)
    add_test(NAME downspout_m_mix_core_tests COMMAND downspout_m_mix_core_tests)
endif()
