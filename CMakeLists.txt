cmake_minimum_required(VERSION 3.20)

project(downspout VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DOWNSPOUT_BUILD_BASSGEN "Configure the bassgen port target" ON)
option(DOWNSPOUT_BUILD_PMIX "Configure the p-mix port target" ON)
option(DOWNSPOUT_ENABLE_DPF "Enable DPF-based plugin wrapper targets" ON)

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

if(DOWNSPOUT_BUILD_PMIX)
    add_subdirectory(plugins/p-mix)
endif()
