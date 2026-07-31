add_library(downspout_mixgen_core STATIC src/mixgen_core.cpp)
target_link_libraries(downspout_mixgen_core PUBLIC downspout-project-options)
target_include_directories(downspout_mixgen_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include")

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(mixgen
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS} ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/MixgenPlugin.cpp
        FILES_UI src/dpf/MixgenUI.cpp)
    target_link_libraries(mixgen-dsp PUBLIC downspout_mixgen_core)
    target_link_libraries(mixgen-ui PUBLIC downspout_mixgen_core)
    target_include_directories(mixgen PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
    target_include_directories(mixgen-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf")
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/mixgen.vst3" DESTINATION "." OPTIONAL)
endif()

if(BUILD_TESTING)
    add_executable(downspout_mixgen_core_tests tests/mixgen_core_tests.cpp)
    target_link_libraries(downspout_mixgen_core_tests PRIVATE downspout_mixgen_core)
    add_test(NAME downspout_mixgen_core_tests COMMAND downspout_mixgen_core_tests)
    if(TARGET downspout_t_mix_core)
        add_executable(downspout_mixgen_t_mix_contract_tests
            tests/mixgen_t_mix_contract_tests.cpp)
        target_link_libraries(downspout_mixgen_t_mix_contract_tests PRIVATE
            downspout_mixgen_core downspout_t_mix_core)
        add_test(NAME downspout_mixgen_t_mix_contract_tests
            COMMAND downspout_mixgen_t_mix_contract_tests)
    endif()
endif()
