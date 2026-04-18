cmake_minimum_required(VERSION 3.20)

project(downspout VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOWNSPOUT_BUILD_BASSGEN "Configure the bassgen port target" ON)
option(DOWNSPOUT_BUILD_PMIX "Configure the p-mix port target" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(downspout-project-options INTERFACE)
target_include_directories(downspout-project-options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(downspout-project-options INTERFACE cxx_std_20)

if(DOWNSPOUT_BUILD_BASSGEN)
    add_subdirectory(plugins/bassgen)
endif()

if(DOWNSPOUT_BUILD_PMIX)
    add_subdirectory(plugins/p-mix)
endif()
