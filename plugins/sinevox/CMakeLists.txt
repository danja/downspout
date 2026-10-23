add_library(downspout_sinevox_core STATIC src/sinevox_engine.cpp)
target_link_libraries(downspout_sinevox_core PUBLIC downspout-project-options)
target_include_directories(downspout_sinevox_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(sinevox
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/SinevoxPlugin.cpp
        FILES_UI  src/dpf/SinevoxUI.cpp
    )
    target_link_libraries(sinevox-dsp PUBLIC downspout_sinevox_core)
    target_include_directories(sinevox PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(sinevox-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/sinevox.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_sinevox_core_tests
        SOURCES tests/sinevox_core_tests.cpp
        LIBS downspout_sinevox_core)
endif()
