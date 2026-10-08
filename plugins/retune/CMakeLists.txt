add_library(downspout_retune_core STATIC
    src/retune_scale.cpp
    src/retune_engine.cpp
)
target_link_libraries(downspout_retune_core PUBLIC downspout-project-options)
target_include_directories(downspout_retune_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(retune
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP
            src/dpf/RetunePlugin.cpp
        FILES_UI
            src/dpf/RetuneUI.cpp
    )
    target_link_libraries(retune-dsp PUBLIC downspout_retune_core)
    target_link_libraries(retune-ui PUBLIC downspout-project-options downspout_retune_core)
    target_include_directories(retune PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(retune-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/retune.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_retune_core_tests tests/retune_core_tests.cpp)
    target_link_libraries(downspout_retune_core_tests PRIVATE downspout_retune_core)
    # Release adds -DNDEBUG, which compiles every assert() out and lets the
    # suite pass while testing nothing.
    if(MSVC)
        target_compile_options(downspout_retune_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_retune_core_tests PRIVATE -UNDEBUG)
    endif()
    target_include_directories(downspout_retune_core_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_definitions(downspout_retune_core_tests PRIVATE RETUNE_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scales")
    add_test(NAME downspout_retune_core_tests COMMAND downspout_retune_core_tests)
endif()
