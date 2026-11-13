add_library(downspout_bore_core STATIC
    src/bore_engine.cpp
)
target_link_libraries(downspout_bore_core PUBLIC downspout-project-options)
target_include_directories(downspout_bore_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(bore
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/BorePlugin.cpp
        FILES_UI
            src/dpf/BoreUI.cpp
    )
    target_link_libraries(bore-dsp PUBLIC downspout_bore_core)
    target_link_libraries(bore-ui PUBLIC downspout_bore_core)
    target_include_directories(bore PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(bore-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/bore.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_bore_core_tests
        SOURCES tests/bore_core_tests.cpp
        LIBS downspout_bore_core)
endif()
