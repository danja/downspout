add_library(downspout_harmonic_atlas_core STATIC src/harmonic_atlas_core.cpp)
target_link_libraries(downspout_harmonic_atlas_core PUBLIC downspout-project-options)
target_include_directories(downspout_harmonic_atlas_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include")
if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(harmonic_atlas TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/HarmonicAtlasPlugin.cpp FILES_UI src/dpf/HarmonicAtlasUI.cpp)
    target_link_libraries(harmonic_atlas-dsp PUBLIC downspout_harmonic_atlas_core)
    target_link_libraries(harmonic_atlas-ui PUBLIC downspout_harmonic_atlas_core)
    target_include_directories(harmonic_atlas PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
    target_include_directories(harmonic_atlas-ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/harmonic_atlas.vst3" DESTINATION "." OPTIONAL)
endif()
if(BUILD_TESTING)
    add_executable(downspout_harmonic_atlas_core_tests tests/harmonic_atlas_core_tests.cpp)
    target_link_libraries(downspout_harmonic_atlas_core_tests PRIVATE downspout_harmonic_atlas_core)
    add_test(NAME downspout_harmonic_atlas_core_tests COMMAND downspout_harmonic_atlas_core_tests)
endif()
