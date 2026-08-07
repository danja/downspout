cmake_minimum_required(VERSION 3.20)

project(downspout VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DOWNSPOUT_BUILD_GATER "Configure the Gater MIDI-controlled switcher target" ON)
option(DOWNSPOUT_BUILD_BASSGEN "Configure the bassgen port target" ON)
option(DOWNSPOUT_BUILD_PMIX "Configure the p-mix port target" ON)
option(DOWNSPOUT_BUILD_EMIX "Configure the e-mix port target" ON)
option(DOWNSPOUT_BUILD_MMIX "Configure the m-mix MIDI target" ON)
option(DOWNSPOUT_BUILD_TMIX "Configure the t-mix eight-input mixer target" ON)
option(DOWNSPOUT_BUILD_MIXGEN "Configure the Mixgen automatic producer target" ON)
option(DOWNSPOUT_BUILD_LOOPDELAY "Configure the Loopdelay stereo delay/looper target" ON)
option(DOWNSPOUT_BUILD_LIGHTVERB "Configure the Lightverb low-CPU reverb target" ON)
option(DOWNSPOUT_BUILD_MELGEN "Configure the melgen MIDI melody target" ON)
option(DOWNSPOUT_BUILD_RIFT "Configure the rift original effect target" ON)
option(DOWNSPOUT_BUILD_ORCHID "Configure the orchid voiced freeze effect target" ON)
option(DOWNSPOUT_BUILD_AMBO "Configure the ambo original ambient effect target" ON)
option(DOWNSPOUT_BUILD_DRUMGEN "Configure the drumgen port target" ON)
option(DOWNSPOUT_BUILD_DRUMKIT "Configure the drumkit port target" ON)
option(DOWNSPOUT_BUILD_SYRINX "Configure the Syrinx birdsong synthesis target" ON)
option(DOWNSPOUT_BUILD_CADENCE "Configure the cadence port target" ON)
option(DOWNSPOUT_BUILD_ARPGEN "Configure the arpgen MIDI arpeggiator target" ON)
option(DOWNSPOUT_BUILD_COUNTERPOINTER "Configure the counterpointer MIDI target" ON)
option(DOWNSPOUT_BUILD_SIDECAR "Configure the Sidecar AI phrase target" ON)
option(DOWNSPOUT_BUILD_AI_COORDINATOR "Configure the local AI coordinator tool target" ON)
option(DOWNSPOUT_BUILD_GREMLIN "Configure the gremlin port target" ON)
option(DOWNSPOUT_BUILD_GREMLIN_DRIVER "Configure the gremlin-driver port target" ON)
option(DOWNSPOUT_BUILD_GROUND "Configure the ground original MIDI target" ON)
option(DOWNSPOUT_BUILD_FLOOZY "Configure the floozy port target" ON)
option(DOWNSPOUT_BUILD_BASILICO "Configure the basilico bass instrument target" ON)
option(DOWNSPOUT_BUILD_CANTICLE "Configure the canticle polyphonic instrument target" ON)
option(DOWNSPOUT_BUILD_LUMA "Configure the luma Launchpad performance target" ON)
option(DOWNSPOUT_BUILD_PAUNCHLAD "Configure the PaunchLad Launchpad dub effect target" ON)
option(DOWNSPOUT_BUILD_LIFEFORM "Configure the Lifeform Launchpad Game of Life MIDI target" ON)
option(DOWNSPOUT_BUILD_XOXOLO "Configure the Xoxolo MIDI drum pattern target" ON)
option(DOWNSPOUT_BUILD_TUNEY_VST "Configure the Tuney VST text-to-music instrument target" ON)
option(DOWNSPOUT_BUILD_HARMONIC_ATLAS "Configure the Harmonic Atlas harmony generator target" ON)
option(DOWNSPOUT_BUILD_CONDUCTOR "Configure the Conductor form generator target" ON)
option(DOWNSPOUT_BUILD_DRIFT "Configure the Drift MIDI modulator target" ON)
option(DOWNSPOUT_BUILD_MNEMOSYNE "Configure the Mnemosyne motif processor target" ON)
option(DOWNSPOUT_BUILD_POLYMETER "Configure the Polymeter rhythm generator target" ON)
option(DOWNSPOUT_BUILD_ORACLE "Configure the Oracle listener/response target" ON)
option(DOWNSPOUT_BUILD_MOSAIC "Configure the Mosaic sampler target" ON)
option(DOWNSPOUT_BUILD_RESONANCE_GARDEN "Configure the Resonance Garden effect target" ON)
option(DOWNSPOUT_BUILD_ORBIT "Configure the Orbit spatial effect target" ON)
option(DOWNSPOUT_BUILD_GUARDIAN "Configure the Guardian safety effect target" ON)
option(DOWNSPOUT_ENABLE_DPF "Enable DPF-based plugin wrapper targets" ON)
option(DOWNSPOUT_BUILD_SCREENSHOT_APPS "Build DPF standalone apps for documentation screenshots" OFF)

set(DOWNSPOUT_DPF_PLUGIN_TARGETS vst3)
set(DOWNSPOUT_DPF_PLUGIN_ARGS)
if(DOWNSPOUT_BUILD_SCREENSHOT_APPS)
    list(APPEND DOWNSPOUT_DPF_PLUGIN_TARGETS jack)
    list(APPEND DOWNSPOUT_DPF_PLUGIN_ARGS SKIP_NATIVE_AUDIO_FALLBACK)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(downspout-project-options INTERFACE)
target_include_directories(downspout-project-options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(downspout-project-options INTERFACE cxx_std_20)

set(DOWNSPOUT_DPF_DIR "")
if(DEFINED DPF_ROOT AND EXISTS "${DPF_ROOT}/CMakeLists.txt")
    set(DOWNSPOUT_DPF_DIR "${DPF_ROOT}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/DPF/CMakeLists.txt")
    set(DOWNSPOUT_DPF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/DPF")
endif()

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${DOWNSPOUT_DPF_DIR}" third_party/DPF)
endif()

if(DOWNSPOUT_BUILD_BASSGEN)
    add_subdirectory(plugins/bassgen)
endif()

if(DOWNSPOUT_BUILD_GATER)
    add_subdirectory(plugins/gater)
endif()

if(DOWNSPOUT_BUILD_PMIX)
    add_subdirectory(plugins/p-mix)
endif()

if(DOWNSPOUT_BUILD_EMIX)
    add_subdirectory(plugins/e-mix)
endif()

if(DOWNSPOUT_BUILD_MMIX)
    add_subdirectory(plugins/m-mix)
endif()

if(DOWNSPOUT_BUILD_TMIX)
    add_subdirectory(plugins/t-mix)
endif()

if(DOWNSPOUT_BUILD_MIXGEN)
    add_subdirectory(plugins/mixgen)
endif()

if(DOWNSPOUT_BUILD_LOOPDELAY)
    add_subdirectory(plugins/loopdelay)
endif()

if(DOWNSPOUT_BUILD_LIGHTVERB)
    add_subdirectory(plugins/lightverb)
endif()

if(BUILD_TESTING AND TARGET downspout_mixgen_core AND TARGET downspout_t_mix_core
   AND TARGET downspout_loopdelay_core AND TARGET downspout_lightverb_core)
    add_executable(downspout_producer_bus_contract_tests
        plugins/mixgen/tests/mixgen_t_mix_contract_tests.cpp)
    target_link_libraries(downspout_producer_bus_contract_tests PRIVATE
        downspout_mixgen_core downspout_t_mix_core
        downspout_loopdelay_core downspout_lightverb_core)
    add_test(NAME downspout_producer_bus_contract_tests
        COMMAND downspout_producer_bus_contract_tests)
endif()

if(DOWNSPOUT_BUILD_MELGEN)
    add_subdirectory(plugins/melgen)
endif()

if(DOWNSPOUT_BUILD_RIFT)
    add_subdirectory(plugins/rift)
endif()

if(DOWNSPOUT_BUILD_ORCHID)
    add_subdirectory(plugins/orchid)
endif()

if(DOWNSPOUT_BUILD_AMBO)
    add_subdirectory(plugins/ambo)
endif()

if(DOWNSPOUT_BUILD_DRUMGEN)
    add_subdirectory(plugins/drumgen)
endif()

if(DOWNSPOUT_BUILD_DRUMKIT)
    add_subdirectory(plugins/drumkit)
endif()

if(DOWNSPOUT_BUILD_SYRINX)
    add_subdirectory(plugins/syrinx)
endif()

if(DOWNSPOUT_BUILD_CADENCE)
    add_subdirectory(plugins/cadence)
endif()

if(DOWNSPOUT_BUILD_ARPGEN)
    add_subdirectory(plugins/arpgen)
endif()

if(DOWNSPOUT_BUILD_COUNTERPOINTER)
    add_subdirectory(plugins/counterpointer)
endif()

if(DOWNSPOUT_BUILD_SIDECAR)
    add_subdirectory(plugins/sidecar)
endif()

if(DOWNSPOUT_BUILD_AI_COORDINATOR)
    if(TARGET downspout_sidecar_core)
        add_subdirectory(tools/ai-coordinator)
    else()
        message(WARNING "DOWNSPOUT_BUILD_AI_COORDINATOR requires downspout_sidecar_core; skipping coordinator")
    endif()
endif()

if(DOWNSPOUT_BUILD_GREMLIN)
    add_subdirectory(plugins/gremlin)
endif()

if(DOWNSPOUT_BUILD_GREMLIN_DRIVER)
    add_subdirectory(plugins/gremlin-driver)
endif()

if(DOWNSPOUT_BUILD_GROUND)
    add_subdirectory(plugins/ground)
endif()

if(DOWNSPOUT_BUILD_FLOOZY)
    add_subdirectory(plugins/floozy)
endif()

if(DOWNSPOUT_BUILD_BASILICO)
    add_subdirectory(plugins/basilico)
endif()

if(DOWNSPOUT_BUILD_CANTICLE)
    add_subdirectory(plugins/canticle)
endif()

if(DOWNSPOUT_BUILD_LUMA)
    add_subdirectory(plugins/luma)
endif()

if(DOWNSPOUT_BUILD_PAUNCHLAD)
    add_subdirectory(plugins/paunchlad)
endif()

if(DOWNSPOUT_BUILD_LIFEFORM)
    add_subdirectory(plugins/lifeform)
endif()

if(DOWNSPOUT_BUILD_XOXOLO)
    add_subdirectory(plugins/xoxolo)
endif()

if(DOWNSPOUT_BUILD_TUNEY_VST)
    add_subdirectory(plugins/tuney-vst)
endif()

if(DOWNSPOUT_BUILD_HARMONIC_ATLAS)
    add_subdirectory(plugins/harmonic-atlas)
endif()

if(DOWNSPOUT_BUILD_CONDUCTOR)
    add_subdirectory(plugins/conductor)
endif()

if(DOWNSPOUT_BUILD_DRIFT)
    add_subdirectory(plugins/drift)
endif()

if(DOWNSPOUT_BUILD_MNEMOSYNE)
    add_subdirectory(plugins/mnemosyne)
endif()

if(DOWNSPOUT_BUILD_POLYMETER)
    add_subdirectory(plugins/polymeter)
endif()

if(DOWNSPOUT_BUILD_ORACLE)
    add_subdirectory(plugins/oracle)
endif()

if(DOWNSPOUT_BUILD_MOSAIC)
    add_subdirectory(plugins/mosaic)
endif()

if(DOWNSPOUT_BUILD_RESONANCE_GARDEN)
    add_subdirectory(plugins/resonance-garden)
endif()

if(DOWNSPOUT_BUILD_ORBIT)
    add_subdirectory(plugins/orbit)
endif()

if(DOWNSPOUT_BUILD_GUARDIAN)
    add_subdirectory(plugins/guardian)
endif()
