add_library(downspout_helterskelter_core STATIC src/helterskelter_core.cpp)
target_link_libraries(downspout_helterskelter_core PUBLIC downspout-project-options)
target_include_directories(downspout_helterskelter_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(helterskelter
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/HelterSkelterPlugin.cpp
        FILES_UI  src/dpf/HelterSkelterUI.cpp
    )
    target_link_libraries(helterskelter-dsp PUBLIC downspout_helterskelter_core)
    target_include_directories(helterskelter PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(helterskelter-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/helterskelter.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    add_executable(downspout_helterskelter_core_tests
        tests/helterskelter_core_tests.cpp
    )
    target_link_libraries(downspout_helterskelter_core_tests PRIVATE downspout_helterskelter_core)
    target_include_directories(downspout_helterskelter_core_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/plugins/generative-common/include"
    )
    add_test(NAME downspout_helterskelter_core_tests COMMAND downspout_helterskelter_core_tests)
endif()
