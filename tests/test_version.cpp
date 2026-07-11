#include "core/version.h"

#include <doctest/doctest.h>

TEST_CASE("core exposes the phase six version") {
    CHECK(chunkmap::version() == "0.6.0-phase6");
}
