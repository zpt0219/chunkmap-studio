#include "../desktop/src/concept_comparison_state.h"

#include <doctest/doctest.h>

using chunkmap_desktop::ConceptComparisonMode;
using chunkmap_desktop::ConceptComparisonState;

TEST_CASE("concept comparison is visible only while its button is held") {
    ConceptComparisonState state;

    state.update_controls(true, false);
    CHECK(state.visible_mode(true) == ConceptComparisonMode::SelectedChunk);
    CHECK(state.visible_mode(false) == ConceptComparisonMode::GeneratedMap);

    state.update_controls(false, true);
    CHECK(state.visible_mode(true) == ConceptComparisonMode::FullMap);
    CHECK(state.visible_mode(false) == ConceptComparisonMode::GeneratedMap);

    state.update_controls(false, false);
    CHECK(state.visible_mode(true) == ConceptComparisonMode::GeneratedMap);
}

TEST_CASE("concept comparison cancellation restores generated images") {
    ConceptComparisonState state;

    for (const auto mode : {ConceptComparisonMode::SelectedChunk,
                            ConceptComparisonMode::FullMap}) {
        state.update_controls(mode == ConceptComparisonMode::SelectedChunk,
                              mode == ConceptComparisonMode::FullMap);
        REQUIRE(state.visible_mode(true) == mode);

        state.cancel();
        CHECK(state.visible_mode(true) == ConceptComparisonMode::GeneratedMap);
    }
}
