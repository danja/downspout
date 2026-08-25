add_library(downspout_sampleprofile STATIC
    sampleprofile.cpp
)
target_include_directories(downspout_sampleprofile PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(downspout_sampleprofile PUBLIC downspout-project-options)

if(BUILD_TESTING)
    add_executable(downspout_sampleprofile_tests
        test_sampleprofile.cpp
    )
    target_link_libraries(downspout_sampleprofile_tests PRIVATE downspout_sampleprofile)
    add_test(NAME downspout_sampleprofile_tests COMMAND downspout_sampleprofile_tests)
endif()
