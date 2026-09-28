add_library(downspout_spliff_core STATIC src/spliff_core.cpp)
target_link_libraries(downspout_spliff_core PUBLIC downspout-project-options)
target_include_directories(downspout_spliff_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(spliff
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/SpliffPlugin.cpp
        FILES_UI  src/dpf/SpliffUI.cpp
    )
    target_link_libraries(spliff-dsp PUBLIC downspout_spliff_core)
    target_include_directories(spliff PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(spliff-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/spliff.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_spliff_core_tests
        tests/spliff_core_tests.cpp
    )
    target_link_libraries(downspout_spliff_core_tests PRIVATE downspout_spliff_core)
    add_test(NAME downspout_spliff_core_tests COMMAND downspout_spliff_core_tests)
endif()
