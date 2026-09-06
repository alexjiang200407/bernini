// A PCH's includes are unused here by construction -- it exists so the sources that do use
// them do not each pay for parsing. It is an optimisation and never an interface: a test
// still includes the Catch2 header it uses. See docs/build_performance.md.
#include <catch2/catch_test_macros.hpp>  // IWYU pragma: keep
