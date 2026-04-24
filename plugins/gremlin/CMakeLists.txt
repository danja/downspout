add_library(downspout_gremlin_core STATIC
    src/gremlin_processor.cpp
)
target_link_libraries(downspout_gremlin_core PUBLIC downspout-project-options)
target_include_directories(downspout_gremlin_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_gremlin_port INTERFACE)
target_link_libraries(downspout_gremlin_port INTERFACE
    downspout-project-options
    downspout_gremlin_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(gremlin
        TARGETS vst3
        FILES_DSP
            src/dpf/GremlinPlugin.cpp
        FILES_UI
            src/dpf/GremlinUI.cpp
    )
    target_link_libraries(gremlin-dsp PUBLIC downspout_gremlin_core)
    target_include_directories(gremlin PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/gremlin.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_gremlin_core_tests
        tests/gremlin_core_tests.cpp
    )
    target_link_libraries(downspout_gremlin_core_tests PRIVATE downspout_gremlin_core)
    add_test(NAME downspout_gremlin_core_tests COMMAND downspout_gremlin_core_tests)
endif()
