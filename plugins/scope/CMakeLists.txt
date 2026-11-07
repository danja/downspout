add_library(downspout_scope_core STATIC
    src/scope_engine.cpp
)
target_link_libraries(downspout_scope_core PUBLIC downspout-project-options)
target_include_directories(downspout_scope_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_scope_port INTERFACE)
target_link_libraries(downspout_scope_port INTERFACE
    downspout-project-options
    downspout_scope_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(scope
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/ScopePlugin.cpp
        FILES_UI
            src/dpf/ScopeUI.cpp
    )
    target_link_libraries(scope-dsp PUBLIC downspout_scope_core)
    target_link_libraries(scope-ui PUBLIC downspout-project-options)
    target_include_directories(scope PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(scope-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/scope.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_scope_core_tests
        SOURCES tests/scope_core_tests.cpp
        LIBS downspout_scope_core)
endif()
