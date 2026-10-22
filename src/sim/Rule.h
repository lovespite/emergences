#pragma once

#include <span>
#include <string_view>
#include <optional>

#include "sim/Agent.h"
#include "sim/World.h"

namespace emergences::sim {

// Borrowed data, valid only during Rule::steer. agent must not be null.
// distanceSquared uses World::displacement (minimum-image distance for Wrap).
// The CPU backend excludes self by array index, includes exact radius matches,
// and passes only this rule's neighbors; radius zero means an empty span.
struct Neighbor {
    const Agent* agent;
    // Keep the query's precision so a second radius filter cannot round an
    // included boundary neighbor outside the rule's neighborhood.
    double distanceSquared;
    // Optional self-to-neighbor minimum-image vector computed by the query.
    // Valid for the same self, positions and world as distanceSquared, rounded
    // exactly like World::displacement. Hand-built neighbors may omit it.
    std::optional<glm::vec3> displacement{};
};

// CPU rule implementation. GPU rules will share parameters and carry a matching
// GLSL implementation; arbitrary C++ callbacks are not executable on the GPU.
// steer returns unweighted acceleration. The backend multiplies by weight once;
// zero weight disables the rule. Weight/radius must be finite and nonnegative.
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
