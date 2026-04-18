add_library(downspout_p_mix_core STATIC
    src/p_mix_engine.cpp
    src/p_mix_serialization.cpp
)
target_link_libraries(downspout_p_mix_core PUBLIC downspout-project-options)
target_include_directories(downspout_p_mix_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(downspout_p_mix_port INTERFACE)
target_link_libraries(downspout_p_mix_port INTERFACE
    downspout-project-options
    downspout_p_mix_core
)

if(BUILD_TESTING)
    add_executable(downspout_p_mix_core_tests
        tests/p_mix_core_tests.cpp
    )
    target_link_libraries(downspout_p_mix_core_tests PRIVATE downspout_p_mix_core)
    add_test(NAME downspout_p_mix_core_tests COMMAND downspout_p_mix_core_tests)
endif()
