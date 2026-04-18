add_library(downspout_p_mix_core STATIC
    src/p_mix_engine.cpp
    src/p_mix_serialization.cpp
)
target_link_libraries(downspout_p_mix_core PUBLIC downspout-project-options)
target_include_directories(downspout_p_mix_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_p_mix_port INTERFACE)
target_link_libraries(downspout_p_mix_port INTERFACE
    downspout-project-options
    downspout_p_mix_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(p_mix
        TARGETS vst3
        FILES_DSP
            src/dpf/PMixPlugin.cpp
        FILES_UI
            src/dpf/PMixUI.cpp
    )
    target_link_libraries(p_mix-dsp PUBLIC downspout_p_mix_core)
    target_include_directories(p_mix PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/p_mix.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_p_mix_core_tests
        tests/p_mix_core_tests.cpp
    )
    target_link_libraries(downspout_p_mix_core_tests PRIVATE downspout_p_mix_core)
    add_test(NAME downspout_p_mix_core_tests COMMAND downspout_p_mix_core_tests)
endif()
