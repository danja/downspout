add_library(downspout_e_mix_core STATIC
    src/e_mix_engine.cpp
    src/e_mix_serialization.cpp
)
target_link_libraries(downspout_e_mix_core PUBLIC downspout-project-options)
target_include_directories(downspout_e_mix_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_e_mix_port INTERFACE)
target_link_libraries(downspout_e_mix_port INTERFACE
    downspout-project-options
    downspout_e_mix_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(e_mix
        TARGETS vst3
        FILES_DSP
            src/dpf/EMixPlugin.cpp
        FILES_UI
            src/dpf/EMixUI.cpp
    )
    target_link_libraries(e_mix-dsp PUBLIC downspout_e_mix_core)
    target_link_libraries(e_mix-ui PUBLIC downspout-project-options)
    target_include_directories(e_mix PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(e_mix-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/e_mix.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_e_mix_core_tests
        tests/e_mix_core_tests.cpp
    )
    target_link_libraries(downspout_e_mix_core_tests PRIVATE downspout_e_mix_core)
    add_test(NAME downspout_e_mix_core_tests COMMAND downspout_e_mix_core_tests)
endif()
