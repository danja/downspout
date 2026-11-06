add_library(downspout_zmorph_core STATIC
    src/zmorph_engine.cpp
)
target_link_libraries(downspout_zmorph_core PUBLIC downspout-project-options)
target_include_directories(downspout_zmorph_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_zmorph_port INTERFACE)
target_link_libraries(downspout_zmorph_port INTERFACE
    downspout-project-options
    downspout_zmorph_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(zmorph
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/ZmorphPlugin.cpp
        FILES_UI
            src/dpf/ZmorphUI.cpp
    )
    target_link_libraries(zmorph-dsp PUBLIC downspout_zmorph_core)
    target_link_libraries(zmorph-ui PUBLIC downspout-project-options)
    target_include_directories(zmorph PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(zmorph-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/zmorph.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_zmorph_core_tests
        SOURCES tests/zmorph_core_tests.cpp
        LIBS downspout_zmorph_core)
endif()