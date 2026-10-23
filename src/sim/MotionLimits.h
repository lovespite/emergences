#pragma once

namespace emergences::sim {
struct MotionLimits {
    float minimumSpeed{1.0F};
    float maximumSpeed{8.0F};
    float maximumAcceleration{20.0F};
};
} // namespace emergences::sim
