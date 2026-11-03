add_library(downspout_fresco_core STATIC src/fresco_core.cpp src/fresco_image.cpp)
target_link_libraries(downspout_fresco_core PUBLIC downspout-project-options)
target_include_directories(downspout_fresco_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(fresco
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP
            src/dpf/FrescoPlugin.cpp
        FILES_UI
            src/dpf/FrescoUI.cpp
    )
    target_link_libraries(fresco-dsp PUBLIC downspout_fresco_core)
    target_link_libraries(fresco-ui PUBLIC downspout-project-options downspout_fresco_core)
    target_include_directories(fresco PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(fresco-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/fresco.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_fresco_core_tests
        SOURCES tests/fresco_core_tests.cpp
        LIBS downspout_fresco_core)
endif()
