add_library(downspout_chorale_core STATIC src/chorale_core.cpp)
target_link_libraries(downspout_chorale_core PUBLIC downspout-project-options)
target_include_directories(downspout_chorale_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(chorale
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/ChoralePlugin.cpp
        FILES_UI
            src/dpf/ChoraleUI.cpp
    )
    target_link_libraries(chorale-dsp PUBLIC downspout_chorale_core)
    target_link_libraries(chorale-ui PUBLIC downspout-project-options downspout_chorale_core)
    target_include_directories(chorale PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(chorale-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/chorale.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_chorale_core_tests
        SOURCES tests/chorale_core_tests.cpp
        LIBS downspout_chorale_core)
endif()
