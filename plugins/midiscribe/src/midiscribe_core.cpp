// midiscribe_core.cpp — translation unit for the midiscribe portable core.
//
// All logic lives in midiscribe_core.hpp (header-only for testability);
// this file exists so the CMake static-library target has at least one
// compilation unit and so that the linker has a home for any future
// non-inline definitions.

#include "midiscribe_core.hpp"
