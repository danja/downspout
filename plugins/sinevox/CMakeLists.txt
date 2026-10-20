add_library(downspout_sinevox_core STATIC src/sinevox_engine.cpp)
target_link_libraries(downspout_sinevox_core PUBLIC downspout-project-options)
target_include_directories(downspout_sinevox_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(sinevox
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/SinevoxPlugin.cpp
        FILES_UI  src/dpf/SinevoxUI.cpp
    )
    target_link_libraries(sinevox-dsp PUBLIC downspout_sinevox_core)
    target_include_directories(sinevox PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(sinevox-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/sinevox.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_sinevox_core_tests
        tests/sinevox_core_tests.cpp
    )
    target_link_libraries(downspout_sinevox_core_tests PRIVATE downspout_sinevox_core)
    # Release builds define NDEBUG, which compiles every assert() in this file
    # out. Without this the suite silently passes while testing nothing.
    if(MSVC)
        target_compile_options(downspout_sinevox_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_sinevox_core_tests PRIVATE -UNDEBUG)
    endif()
    target_include_directories(downspout_sinevox_core_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    add_test(NAME downspout_sinevox_core_tests COMMAND downspout_sinevox_core_tests)
endif()
