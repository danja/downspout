add_library(downspout_gremlin_driver_core STATIC
    src/gremlin_driver_processor.cpp
)
target_link_libraries(downspout_gremlin_driver_core PUBLIC downspout-project-options)
target_include_directories(downspout_gremlin_driver_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_gremlin_driver_port INTERFACE)
target_link_libraries(downspout_gremlin_driver_port INTERFACE
    downspout-project-options
    downspout_gremlin_driver_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(gremlin_driver
        TARGETS vst3
        FILES_DSP
            src/dpf/GremlinDriverPlugin.cpp
        FILES_UI
            src/dpf/GremlinDriverUI.cpp
    )
    target_link_libraries(gremlin_driver-dsp PUBLIC downspout_gremlin_driver_core)
    target_include_directories(gremlin_driver PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/gremlin_driver.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_gremlin_driver_core_tests
        tests/gremlin_driver_core_tests.cpp
    )
    target_link_libraries(downspout_gremlin_driver_core_tests PRIVATE downspout_gremlin_driver_core)
    add_test(NAME downspout_gremlin_driver_core_tests COMMAND downspout_gremlin_driver_core_tests)
endif()
