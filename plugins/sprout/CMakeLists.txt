add_library(downspout_sprout_core STATIC src/sprout_core.cpp)
target_link_libraries(downspout_sprout_core PUBLIC downspout-project-options)
target_include_directories(downspout_sprout_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(sprout
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SproutPlugin.cpp
        FILES_UI
            src/dpf/SproutUI.cpp
    )
    target_link_libraries(sprout-dsp PUBLIC downspout_sprout_core)
    target_link_libraries(sprout-ui PUBLIC downspout_sprout_core)
    target_include_directories(sprout PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(sprout-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/sprout.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_sprout_core_tests
        SOURCES tests/sprout_core_tests.cpp
        LIBS downspout_sprout_core)
endif()
