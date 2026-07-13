#pragma once

namespace chunkmap_desktop {

enum class ConceptComparisonMode {
    GeneratedMap,
    SelectedChunk,
    FullMap,
};

class ConceptComparisonState {
public:
    void update_controls(bool selected_chunk_active, bool full_map_active) {
        if (full_map_active) {
            held_mode_ = ConceptComparisonMode::FullMap;
        } else if (selected_chunk_active) {
            held_mode_ = ConceptComparisonMode::SelectedChunk;
        } else {
            held_mode_ = ConceptComparisonMode::GeneratedMap;
        }
    }

    void cancel() { held_mode_ = ConceptComparisonMode::GeneratedMap; }

    ConceptComparisonMode visible_mode(bool left_mouse_down) const {
        return left_mouse_down ? held_mode_ : ConceptComparisonMode::GeneratedMap;
    }

private:
    ConceptComparisonMode held_mode_ = ConceptComparisonMode::GeneratedMap;
};

}  // namespace chunkmap_desktop
