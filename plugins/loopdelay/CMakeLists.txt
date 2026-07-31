add_library(downspout_loopdelay_core STATIC
    src/loopdelay_core.cpp
    src/loopdelay_serialization.cpp)
target_link_libraries(downspout_loopdelay_core PUBLIC downspout-project-options)
target_include_directories(downspout_loopdelay_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include")

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(loopdelay
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS} ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/LoopdelayPlugin.cpp
        FILES_UI src/dpf/LoopdelayUI.cpp)
    target_link_libraries(loopdelay-dsp PUBLIC downspout_loopdelay_core)
    target_link_libraries(loopdelay-ui PUBLIC downspout_loopdelay_core)
    target_include_directories(loopdelay PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
    target_include_directories(loopdelay-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/loopdelay.vst3" DESTINATION "." OPTIONAL)
endif()

if(BUILD_TESTING)
    add_executable(downspout_loopdelay_core_tests tests/loopdelay_core_tests.cpp)
    target_link_libraries(downspout_loopdelay_core_tests PRIVATE downspout_loopdelay_core)
    add_test(NAME downspout_loopdelay_core_tests COMMAND downspout_loopdelay_core_tests)
endif()
