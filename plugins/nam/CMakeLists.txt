# Linking the vendored NAM archive whole needs generator expressions from CMake 3.24.
if(CMAKE_VERSION VERSION_LESS 3.24)
    message(WARNING "NAM plugin skipped: it needs CMake 3.24 or newer (found ${CMAKE_VERSION}).")
    return()
endif()

# Vendored Neural Amp Modeler Core (MIT) and Eigen (MPL2): see third_party/README.md.
# Built without the project's warning flags; the sources are not ours to fix.
file(GLOB DOWNSPOUT_NAM_VENDOR_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/third_party/NeuralAmpModelerCore/NAM/*.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/NeuralAmpModelerCore/NAM/wavenet/*.cpp"
)
add_library(downspout_nam_vendor STATIC ${DOWNSPOUT_NAM_VENDOR_SOURCES})
target_compile_features(downspout_nam_vendor PUBLIC cxx_std_20)
target_compile_definitions(downspout_nam_vendor PUBLIC NAM_ENABLE_A2_FAST)
target_include_directories(downspout_nam_vendor SYSTEM PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/NeuralAmpModelerCore"
    "${PROJECT_SOURCE_DIR}/third_party/NeuralAmpModelerCore/NAM"
    "${PROJECT_SOURCE_DIR}/third_party/nlohmann"
    "${PROJECT_SOURCE_DIR}/third_party/eigen"
)

add_library(downspout_nam_core STATIC
    src/nam_engine.cpp
)
# NAM registers each architecture (WaveNet, LSTM, ...) from a static initializer in its own
# translation unit. Nothing references those objects, so a plain static link drops them and every
# model then fails with "No config parser registered": link the archive whole.
target_link_libraries(downspout_nam_core PUBLIC
    downspout-project-options
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,downspout_nam_vendor>"
)
target_include_directories(downspout_nam_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(nam
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP
            src/dpf/NamPlugin.cpp
        FILES_UI
            src/dpf/NamUI.cpp
    )
    target_link_libraries(nam-dsp PUBLIC downspout_nam_core)
    target_link_libraries(nam-ui PUBLIC downspout-project-options downspout_nam_core)
    target_include_directories(nam PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(nam-ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dpf"
        "${PROJECT_SOURCE_DIR}/include"
    )

    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/nam.vst3"
        DESTINATION "."
        OPTIONAL
    )
endif()

if(BUILD_TESTING)
    downspout_add_core_test(downspout_nam_core_tests
        SOURCES tests/nam_core_tests.cpp
        LIBS downspout_nam_core
        DEFINITIONS NAM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
