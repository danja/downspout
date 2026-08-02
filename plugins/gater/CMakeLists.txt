add_library(downspout_gater_core STATIC
    src/gater_engine.cpp
)
target_link_libraries(downspout_gater_core PUBLIC downspout-project-options)
target_include_directories(downspout_gater_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(gater
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        FILES_DSP
            src/dpf/GaterPlugin.cpp
        FILES_UI
            src/dpf/GaterUI.cpp
    )
    target_link_libraries(gater-dsp PUBLIC downspout_gater_core)
    target_link_libraries(gater-ui PUBLIC downspout-project-options)
    target_include_directories(gater PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )
    target_include_directories(gater-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/gater.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()
