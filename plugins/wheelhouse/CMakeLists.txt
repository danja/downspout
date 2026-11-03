add_library(downspout_wheelhouse_core STATIC src/wheelhouse_core.cpp)
target_link_libraries(downspout_wheelhouse_core PUBLIC downspout-project-options)
target_include_directories(downspout_wheelhouse_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(wheelhouse
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/WheelhousePlugin.cpp
        FILES_UI
            src/dpf/WheelhouseUI.cpp
    )
    target_link_libraries(wheelhouse-dsp PUBLIC downspout_wheelhouse_core)
    target_link_libraries(wheelhouse-ui PUBLIC downspout-project-options downspout_wheelhouse_core)
    target_include_directories(wheelhouse PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(wheelhouse-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/wheelhouse.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_wheelhouse_core_tests
        SOURCES tests/wheelhouse_core_tests.cpp
        LIBS downspout_wheelhouse_core)
endif()
