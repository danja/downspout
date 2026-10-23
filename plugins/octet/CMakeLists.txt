add_library(downspout_octet_core STATIC src/octet_core.cpp src/octet_spectrum.cpp)
target_link_libraries(downspout_octet_core PUBLIC downspout-project-options)
target_include_directories(downspout_octet_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(octet
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/OctetPlugin.cpp
        FILES_UI
            src/dpf/OctetUI.cpp
    )
    target_link_libraries(octet-dsp PUBLIC downspout_octet_core)
    target_link_libraries(octet-ui PUBLIC downspout-project-options downspout_octet_core)
    target_include_directories(octet PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(octet-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/octet.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_octet_core_tests
        SOURCES tests/octet_core_tests.cpp
        LIBS downspout_octet_core)
endif()
