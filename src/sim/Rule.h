#pragma once

#include <span>
#include <string_view>

#include "sim/Agent.h"
#include "sim/World.h"

namespace emergences::sim {

// Borrowed data, valid only during Rule::steer. agent must not be null.
struct Neighbor {
    const Agent* agent;
    float distanceSquared;
};

// CPU rule implementation. GPU rules will share parameters and carry a matching
// GLSL implementation; arbitrary C++ callbacks are not executable on the GPU.
class Rule {
public:
    virtual ~Rule() = default;

    [[nodiscard]] virtual glm::vec3 steer(
        const Agent& self,
        std::span<const Neighbor> neighbors,
        const World& world) const = 0;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual float weight() const noexcept = 0;
    [[nodiscard]] virtual float radius() const noexcept = 0;
    virtual void setWeight(float weight) = 0;
    virtual void setRadius(float radius) = 0;
};

} // namespace emergences::sim
