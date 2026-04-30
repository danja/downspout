add_library(downspout_rift_core STATIC
    src/rift_engine.cpp
    src/rift_serialization.cpp
)
target_link_libraries(downspout_rift_core PUBLIC downspout-project-options)
target_include_directories(downspout_rift_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_rift_port INTERFACE)
target_link_libraries(downspout_rift_port INTERFACE
    downspout-project-options
    downspout_rift_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(rift
        TARGETS vst3
        FILES_DSP
            src/dpf/RiftPlugin.cpp
        FILES_UI
            src/dpf/RiftUI.cpp
    )
    target_link_libraries(rift-dsp PUBLIC downspout_rift_core)
    target_link_libraries(rift-ui PUBLIC downspout-project-options)
    target_include_directories(rift PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(rift-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/rift.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_rift_core_tests
        tests/rift_core_tests.cpp
    )
    target_link_libraries(downspout_rift_core_tests PRIVATE downspout_rift_core)
    add_test(NAME downspout_rift_core_tests COMMAND downspout_rift_core_tests)
endif()
