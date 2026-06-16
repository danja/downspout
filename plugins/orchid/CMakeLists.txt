add_library(downspout_orchid_core STATIC
    src/orchid_engine.cpp
    src/orchid_serialization.cpp
)
target_link_libraries(downspout_orchid_core PUBLIC downspout-project-options)
target_include_directories(downspout_orchid_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_orchid_port INTERFACE)
target_link_libraries(downspout_orchid_port INTERFACE
    downspout-project-options
    downspout_orchid_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(orchid
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/OrchidPlugin.cpp
        FILES_UI
            src/dpf/OrchidUI.cpp
    )
    target_link_libraries(orchid-dsp PUBLIC downspout_orchid_core)
    target_link_libraries(orchid-ui PUBLIC downspout-project-options)
    target_include_directories(orchid PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(orchid-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/orchid.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_orchid_core_tests
        tests/orchid_core_tests.cpp
    )
    target_link_libraries(downspout_orchid_core_tests PRIVATE downspout_orchid_core)
    add_test(NAME downspout_orchid_core_tests COMMAND downspout_orchid_core_tests)
endif()
