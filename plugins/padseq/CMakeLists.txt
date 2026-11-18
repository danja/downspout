add_library(downspout_padseq_core STATIC
    src/padseq_core.cpp
)
target_link_libraries(downspout_padseq_core PUBLIC downspout-project-options)
target_include_directories(downspout_padseq_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_padseq_port INTERFACE)
target_link_libraries(downspout_padseq_port INTERFACE
    downspout-project-options
    downspout_padseq_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(padseq
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/PadSeqPlugin.cpp
        FILES_UI
            src/dpf/PadSeqUI.cpp
    )
    target_link_libraries(padseq-dsp PUBLIC downspout_padseq_core)
    target_link_libraries(padseq-ui PUBLIC downspout-project-options)
    target_include_directories(padseq PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(padseq-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include"
    )
endif()

downspout_add_core_test(downspout_padseq_core_tests
    SOURCES
        tests/padseq_core_tests.cpp
    LIBS
        downspout_padseq_core
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/padseq.vst3"
        DESTINATION "${DOWNSPOUT_PLUGIN_INSTALL_DIR}"
        OPTIONAL
    )
endif()
