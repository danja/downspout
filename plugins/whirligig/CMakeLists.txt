add_library(downspout_whirligig_core STATIC src/whirligig_core.cpp)
target_link_libraries(downspout_whirligig_core PUBLIC downspout-project-options)
target_include_directories(downspout_whirligig_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(whirligig
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/WhirligigPlugin.cpp
        FILES_UI
            src/dpf/WhirligigUI.cpp
    )
    target_link_libraries(whirligig-dsp PUBLIC downspout_whirligig_core)
    target_link_libraries(whirligig-ui PUBLIC downspout-project-options downspout_whirligig_core)
    target_include_directories(whirligig PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(whirligig-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/whirligig.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_whirligig_core_tests
        SOURCES tests/whirligig_core_tests.cpp
        LIBS downspout_whirligig_core)
endif()
