#pragma once

#include <algorithm>
#include <array>

namespace chunkmap_desktop {

struct MapPoint {
    float x = 0.0F;
    float y = 0.0F;
};

class MapZoomState {
public:
    static constexpr std::array<float, 13> scales = {
        0.125F, 0.25F, 0.5F, 1.0F, 2.0F, 3.0F, 4.0F,
        6.0F, 8.0F, 12.0F, 16.0F, 24.0F, 32.0F};

    float scale() const { return scale_; }
    float pan_x() const { return pan_x_; }
    float pan_y() const { return pan_y_; }

    void reset() {
        scale_ = 1.0F;
        pan_x_ = 0.0F;
        pan_y_ = 0.0F;
    }

    void fit(float canvas_width,
             float canvas_height,
             float world_width,
             float world_height,
             float margin = 0.0F) {
        const float usable_width = std::max(1.0F, canvas_width - margin * 2.0F);
        const float usable_height = std::max(1.0F, canvas_height - margin * 2.0F);
        scale_ = std::min(
            usable_width / std::max(1.0F, world_width),
            usable_height / std::max(1.0F, world_height));
        pan_x_ = (world_width - canvas_width / scale_) * 0.5F;
        pan_y_ = (world_height - canvas_height / scale_) * 0.5F;
    }

    void focus(float canvas_width,
               float canvas_height,
               float center_x,
               float center_y,
               float target_width,
               float target_height,
               float padding = 1.0F) {
        scale_ = std::min(
            canvas_width / std::max(1.0F, target_width * padding),
            canvas_height / std::max(1.0F, target_height * padding));
        pan_x_ = center_x - canvas_width / (2.0F * scale_);
        pan_y_ = center_y - canvas_height / (2.0F * scale_);
    }

    void pan_by_screen(float delta_x, float delta_y) {
        pan_x_ -= delta_x / scale_;
        pan_y_ -= delta_y / scale_;
    }

    MapPoint screen_to_world(float screen_x, float screen_y) const {
        return {pan_x_ + screen_x / scale_, pan_y_ + screen_y / scale_};
    }

    bool step_at(float wheel, float screen_x, float screen_y) {
        if (wheel == 0.0F) return false;
        constexpr float epsilon = 0.0001F;
        float next = scale_;
        if (wheel > 0.0F) {
            const auto found = std::find_if(scales.begin(), scales.end(), [&](float value) {
                return value > scale_ + epsilon;
            });
            if (found == scales.end()) return false;
            next = *found;
        } else {
            const auto found = std::find_if(scales.rbegin(), scales.rend(), [&](float value) {
                return value < scale_ - epsilon;
            });
            if (found == scales.rend()) return false;
            next = *found;
        }
        const MapPoint anchor = screen_to_world(screen_x, screen_y);
        scale_ = next;
        pan_x_ = anchor.x - screen_x / scale_;
        pan_y_ = anchor.y - screen_y / scale_;
        return true;
    }

private:
    float scale_ = 1.0F;
    float pan_x_ = 0.0F;
    float pan_y_ = 0.0F;
};

}  // namespace chunkmap_desktop
