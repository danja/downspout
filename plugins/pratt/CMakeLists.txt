add_library(downspout_pratt_core STATIC
    src/pratt_poly.cpp
    src/pratt_wavetable.cpp
    src/pratt_engine.cpp
    src/pratt_serialization.cpp
)
target_link_libraries(downspout_pratt_core PUBLIC downspout-project-options)
target_include_directories(downspout_pratt_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_pratt_port INTERFACE)
target_link_libraries(downspout_pratt_port INTERFACE
    downspout-project-options
    downspout_pratt_core
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(pratt
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/PrattPlugin.cpp
        FILES_UI
            src/dpf/PrattUI.cpp
    )
    target_link_libraries(pratt-dsp PUBLIC downspout_pratt_core)
    target_link_libraries(pratt-ui PUBLIC downspout-project-options downspout_pratt_core)
    target_include_directories(pratt PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(pratt-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/pratt.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    foreach(suite core engine params threading)
        add_executable(downspout_pratt_${suite}_tests
            tests/pratt_${suite}_tests.cpp
        )
        target_link_libraries(downspout_pratt_${suite}_tests PRIVATE downspout_pratt_core)
        # Release adds -DNDEBUG, which compiles every assert() out and lets the
        # suite pass while testing nothing.
        if(MSVC)
            target_compile_options(downspout_pratt_${suite}_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(downspout_pratt_${suite}_tests PRIVATE -UNDEBUG)
        endif()
        target_include_directories(downspout_pratt_${suite}_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
        add_test(NAME downspout_pratt_${suite}_tests COMMAND downspout_pratt_${suite}_tests)
    endforeach()
endif()
