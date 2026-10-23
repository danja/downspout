add_library(downspout_fathom_core STATIC
    src/fathom_generator.cpp
    src/fathom_processor.cpp
    src/fathom_engine.cpp
)
target_link_libraries(downspout_fathom_core PUBLIC downspout-project-options)
target_include_directories(downspout_fathom_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(fathom
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/FathomPlugin.cpp
        FILES_UI
            src/dpf/FathomUI.cpp
    )
    target_link_libraries(fathom-dsp PUBLIC downspout_fathom_core)
    target_link_libraries(fathom-ui PUBLIC downspout_fathom_core)
    target_include_directories(fathom PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(fathom-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/fathom.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    # Drift's lane table is inline constexpr data; the CC-alignment test reads it
    # directly, so no link dependency on the drift library is needed.
    downspout_add_core_test(downspout_fathom_core_tests
        SOURCES tests/fathom_core_tests.cpp
        LIBS downspout_fathom_core
        INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/plugins/drift/include" "${PROJECT_SOURCE_DIR}/plugins/generative-common/include")
endif()
