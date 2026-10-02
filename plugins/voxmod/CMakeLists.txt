add_library(downspout_voxmod_core STATIC src/voxmod_engine.cpp)
target_link_libraries(downspout_voxmod_core PUBLIC downspout-project-options)
target_include_directories(downspout_voxmod_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(voxmod
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/VoxmodPlugin.cpp
        FILES_UI  src/dpf/VoxmodUI.cpp
    )
    target_link_libraries(voxmod-dsp PUBLIC downspout_voxmod_core)
    target_include_directories(voxmod PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(voxmod-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/voxmod.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_voxmod_core_tests
        tests/voxmod_core_tests.cpp
    )
    target_link_libraries(downspout_voxmod_core_tests PRIVATE downspout_voxmod_core)
    # Release builds define NDEBUG, which compiles every assert() in this file
    # out. Without this the suite silently passes while testing nothing.
    if(MSVC)
        target_compile_options(downspout_voxmod_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_voxmod_core_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME downspout_voxmod_core_tests COMMAND downspout_voxmod_core_tests)
endif()