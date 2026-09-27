add_library(downspout_primefold_core STATIC
    src/primefold_core.cpp
)
target_link_libraries(downspout_primefold_core PUBLIC downspout-project-options)
target_include_directories(downspout_primefold_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(primefold
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/PrimefoldPlugin.cpp
        FILES_UI
            src/dpf/PrimefoldUI.cpp
    )
    target_link_libraries(primefold-dsp PUBLIC downspout_primefold_core)
    target_link_libraries(primefold-ui PUBLIC downspout-project-options)
    target_include_directories(primefold PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(primefold-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/primefold.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_primefold_core_tests
        tests/primefold_core_tests.cpp
    )
    target_link_libraries(downspout_primefold_core_tests PRIVATE downspout_primefold_core)
    add_test(NAME downspout_primefold_core_tests COMMAND downspout_primefold_core_tests)
endif()
