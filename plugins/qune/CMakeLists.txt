add_library(downspout_qune_core STATIC
    src/qune_pitch.cpp
    src/qune_fast.cpp
    src/qune_psola.cpp
    src/qune_spectral.cpp
    src/qune_engine.cpp
)
target_link_libraries(downspout_qune_core PUBLIC downspout-project-options)
target_include_directories(downspout_qune_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_qune_port INTERFACE)
target_link_libraries(downspout_qune_port INTERFACE
    downspout-project-options
    downspout_qune_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(qune
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/QunePlugin.cpp
        FILES_UI
            src/dpf/QuneUI.cpp
    )
    target_link_libraries(qune-dsp PUBLIC downspout_qune_core)
    target_link_libraries(qune-ui PUBLIC downspout-project-options)
    target_include_directories(qune PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(qune-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/qune.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_qune_core_tests
        tests/qune_core_tests.cpp
    )
    target_link_libraries(downspout_qune_core_tests PRIVATE downspout_qune_core)
    # Release adds -DNDEBUG, which compiles every assert() out and lets the
    # suite pass while testing nothing.
    if(MSVC)
        target_compile_options(downspout_qune_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_qune_core_tests PRIVATE -UNDEBUG)
    endif()
    target_include_directories(downspout_qune_core_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    add_test(NAME downspout_qune_core_tests COMMAND downspout_qune_core_tests)
endif()
