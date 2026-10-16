add_library(downspout_trajectory_core STATIC
    src/trajectory_oscillator.cpp
    src/trajectory_engine.cpp
)
target_link_libraries(downspout_trajectory_core PUBLIC downspout-project-options)
target_include_directories(downspout_trajectory_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(trajectory
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/TrajectoryPlugin.cpp
        FILES_UI
            src/dpf/TrajectoryUI.cpp
    )
    target_link_libraries(trajectory-dsp PUBLIC downspout_trajectory_core)
    target_link_libraries(trajectory-ui PUBLIC downspout_trajectory_core)
    target_include_directories(trajectory PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(trajectory-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/trajectory.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_trajectory_core_tests tests/trajectory_core_tests.cpp)
    target_link_libraries(downspout_trajectory_core_tests PRIVATE downspout_trajectory_core)
    # The suite uses its own require() rather than assert(), so the Release
    # build's -DNDEBUG cannot compile the checks out. -UNDEBUG is belt and braces.
    if(MSVC)
        target_compile_options(downspout_trajectory_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_trajectory_core_tests PRIVATE -UNDEBUG)
    endif()
    # Drift's lane table is inline constexpr data; the CC-alignment test reads it
    # directly, so no link dependency on the drift library is needed.
    target_include_directories(downspout_trajectory_core_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/plugins/drift/include"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
    )
    add_test(NAME downspout_trajectory_core_tests COMMAND downspout_trajectory_core_tests)
endif()
