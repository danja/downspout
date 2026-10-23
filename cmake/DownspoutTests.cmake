# One place for what every portable core test suite needs, so a new plugin cannot forget it.
#
#   downspout_add_core_test(<target>
#       SOURCES <files...>
#       LIBS <libraries...>
#       [INCLUDE_DIRS <dirs...>]      # in addition to ${PROJECT_SOURCE_DIR}/include
#       [DEFINITIONS <defs...>])
#
# Builds the executable, links the libraries, registers a ctest named after the target, and:
#  * undefines NDEBUG: a Release build adds -DNDEBUG, which compiles every assert() out and lets a
#    suite pass while testing nothing (include/downspout/test_assert.h turns a lost -UNDEBUG into a
#    compile error for suites that use assert());
#  * adds the shared include directory (downspout/test_assert.h and the other shared headers).
function(downspout_add_core_test target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;INCLUDE_DIRS;DEFINITIONS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "downspout_add_core_test(${target}): SOURCES is required")
    endif()
    add_executable(${target} ${ARG_SOURCES})
    if(ARG_LIBS)
        target_link_libraries(${target} PRIVATE ${ARG_LIBS})
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include" ${ARG_INCLUDE_DIRS})
    if(ARG_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${ARG_DEFINITIONS})
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()
