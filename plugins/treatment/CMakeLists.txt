add_library(downspout_treatment_core STATIC src/treatment_engine.cpp)
target_link_libraries(downspout_treatment_core PUBLIC downspout-project-options)
target_include_directories(downspout_treatment_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(treatment
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP src/dpf/TreatmentPlugin.cpp
        FILES_UI  src/dpf/TreatmentUI.cpp
    )
    target_link_libraries(treatment-dsp PUBLIC downspout_treatment_core)
    target_include_directories(treatment PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(treatment-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/treatment.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_treatment_core_tests
        SOURCES tests/treatment_core_tests.cpp
        LIBS downspout_treatment_core)
endif()
