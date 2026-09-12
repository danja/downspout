add_library(downspout_magneto_core STATIC
    src/magneto_engine.cpp
    src/magneto_serialization.cpp
)
target_link_libraries(downspout_magneto_core PUBLIC downspout-project-options)
target_include_directories(downspout_magneto_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_magneto_port INTERFACE)
target_link_libraries(downspout_magneto_port INTERFACE
    downspout-project-options
    downspout_magneto_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(magneto
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/MagnetoPlugin.cpp
        FILES_UI
            src/dpf/MagnetoUI.cpp
    )
    target_link_libraries(magneto-dsp PUBLIC downspout_magneto_core)
    target_link_libraries(magneto-ui PUBLIC downspout-project-options)
    target_include_directories(magneto PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(magneto-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/magneto.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_magneto_core_tests
        tests/magneto_core_tests.cpp
    )
    target_link_libraries(downspout_magneto_core_tests PRIVATE downspout_magneto_core)
    add_test(NAME downspout_magneto_core_tests COMMAND downspout_magneto_core_tests)
endif()
