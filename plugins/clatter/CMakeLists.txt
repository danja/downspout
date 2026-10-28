add_library(downspout_clatter_core STATIC
    src/clatter_engine.cpp
    src/clatter_serialization.cpp
)
target_link_libraries(downspout_clatter_core PUBLIC downspout-project-options)
target_include_directories(downspout_clatter_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(clatter
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/ClatterPlugin.cpp
        FILES_UI
            src/dpf/ClatterUI.cpp
    )
    target_link_libraries(clatter-dsp PUBLIC downspout_clatter_core)
    target_link_libraries(clatter-ui PUBLIC downspout-project-options downspout_clatter_core)
    target_include_directories(clatter PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(clatter-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/clatter.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    # The coverage test checks Clatter's notes against the other drum plugins' maps, for whichever are built.
    set(_clatter_test_libs downspout_clatter_core)
    set(_clatter_test_defs)
    foreach(_drum thumper snatch tops)
        if(TARGET downspout_${_drum}_core)
            list(APPEND _clatter_test_libs downspout_${_drum}_core)
            string(TOUPPER ${_drum} _drum_upper)
            list(APPEND _clatter_test_defs CLATTER_TEST_HAS_${_drum_upper})
        endif()
    endforeach()
    downspout_add_core_test(downspout_clatter_core_tests
        SOURCES tests/clatter_core_tests.cpp
        LIBS ${_clatter_test_libs})
    target_compile_definitions(downspout_clatter_core_tests PRIVATE ${_clatter_test_defs})
endif()
