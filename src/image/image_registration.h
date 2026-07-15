#pragma once

#include "core/result.h"
#include "image/image_pipeline.h"

namespace chunkmap {

struct ImageRegistrationLimits {
    int maximum_x = 0;
    int maximum_y = 0;
};

enum class ImageRegistrationMethod {
    LowResolution2D,
    Projection,
};

struct ImageRegistrationCandidate {
    ImageRegistrationMethod method = ImageRegistrationMethod::LowResolution2D;
    int offset_x = 0;
    int offset_y = 0;
    double score = 0.0;
    double relative_improvement = 0.0;
    bool accepted = false;
    bool evaluated = false;
};

struct ImageRegistrationComparison {
    ImageRegistrationCandidate low_resolution;
    ImageRegistrationCandidate projection;
    ImageRegistrationMethod selected_method = ImageRegistrationMethod::LowResolution2D;
};

struct ImageRegistrationResult {
    ImageBuffer image;
    int offset_x = 0;
    int offset_y = 0;
    double score_before = 0.0;
    double score_after = 0.0;
    double relative_improvement = 0.0;
    bool applied = false;
    ImageRegistrationComparison comparison;
};

const char* registration_method_name(ImageRegistrationMethod method);

class ImageRegistration {
public:
    static Result<ImageRegistrationLimits> limits(const ProjectConfig& config);
    static Result<ImageRegistrationResult> align(
        const ImageBuffer& source,
        const ProjectConfig& config,
        const NeighborImages& neighbors);
    static Result<ImageBuffer> translate(
        const ImageBuffer& source,
        const ProjectConfig& config,
        int offset_x,
        int offset_y);
};

}  // namespace chunkmap
