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
    add_executable(downspout_sprout_core_tests tests/sprout_core_tests.cpp)
    target_link_libraries(downspout_sprout_core_tests PRIVATE downspout_sprout_core)
    # Release adds -DNDEBUG, which compiles every assert() out and lets the
    # suite pass while testing nothing.
    if(MSVC)
        target_compile_options(downspout_sprout_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_sprout_core_tests PRIVATE -UNDEBUG)
    endif()
    target_include_directories(downspout_sprout_core_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    add_test(NAME downspout_sprout_core_tests COMMAND downspout_sprout_core_tests)
endif()
