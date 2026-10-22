# Vendored SAM (third_party/sam) is plain C with its own idioms, so it is built
# apart from the Downspout warning flags that downspout-project-options applies.
add_library(downspout_sam_vendor STATIC
    third_party/sam/createtransitions.c
    third_party/sam/debug.c
    third_party/sam/processframes.c
    third_party/sam/reciter.c
    third_party/sam/render.c
    third_party/sam/sam.c
)
target_include_directories(downspout_sam_vendor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sam"
)
set_target_properties(downspout_sam_vendor PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(downspout_sam_vendor PRIVATE -w)
endif()

add_library(downspout_sam_core STATIC
    src/sam_speech.cpp
    src/sam_engine.cpp
)
target_link_libraries(downspout_sam_core PUBLIC downspout-project-options downspout_sam_vendor)
target_include_directories(downspout_sam_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
set_target_properties(downspout_sam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(sam
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/SamPlugin.cpp
        FILES_UI
            src/dpf/SamUI.cpp
    )
    target_link_libraries(sam-dsp PUBLIC downspout_sam_core)
    target_link_libraries(sam-ui PUBLIC downspout_sam_core)
    target_include_directories(sam PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(sam-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/sam.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_sam_core_tests tests/sam_core_tests.cpp)
    target_link_libraries(downspout_sam_core_tests PRIVATE downspout_sam_core)
    # Release adds -DNDEBUG, which compiles every assert() out and lets the
    # suite pass while testing nothing.
    if(MSVC)
        target_compile_options(downspout_sam_core_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(downspout_sam_core_tests PRIVATE -UNDEBUG)
    endif()
    target_include_directories(downspout_sam_core_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    add_test(NAME downspout_sam_core_tests COMMAND downspout_sam_core_tests)
endif()
