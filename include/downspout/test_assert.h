#pragma once

// Include this from any test translation unit that relies on assert().
//
// CMAKE_BUILD_TYPE=Release adds -DNDEBUG, which compiles every assert() out, so
// a suite can pass while testing nothing. Test targets must be built with
// -UNDEBUG (see the target_compile_options block next to each add_test). This
// header turns a missing or lost -UNDEBUG into a compile error rather than a
// silent pass.

#ifdef NDEBUG
#error "assert() is compiled out (NDEBUG is defined): add -UNDEBUG to this test target"
#endif

#include <cassert>
