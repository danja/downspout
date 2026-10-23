add_library(downspout_resonance_garden_core STATIC src/resonance_garden_core.cpp)
target_link_libraries(downspout_resonance_garden_core PUBLIC downspout-project-options)
target_include_directories(downspout_resonance_garden_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/plugins/generative-common/include")
if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
dpf_add_plugin(resonance_garden TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS} ${DOWNSPOUT_DPF_PLUGIN_ARGS} FILES_DSP src/dpf/ResonanceGardenPlugin.cpp FILES_UI src/dpf/ResonanceGardenUI.cpp)
target_link_libraries(resonance_garden-dsp PUBLIC downspout_resonance_garden_core)
target_link_libraries(resonance_garden-ui PUBLIC downspout_resonance_garden_core)
target_include_directories(resonance_garden PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/plugins/generative-common/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    "${PROJECT_SOURCE_DIR}/include")
target_include_directories(resonance_garden-ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/plugins/generative-common/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/resonance_garden.vst3" DESTINATION "." OPTIONAL)
endif()
if(BUILD_TESTING)
    downspout_add_core_test(downspout_resonance_garden_core_tests
        SOURCES tests/resonance_garden_core_tests.cpp
        LIBS downspout_resonance_garden_core)
endif()
