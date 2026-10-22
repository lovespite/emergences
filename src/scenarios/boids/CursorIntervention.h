#pragma once

#include "sim/Intervention.h"

namespace emergences::scenarios::boids {

// CPU-only radial cursor force. Position is supplied in world coordinates by
// the application; this class has no camera, input, or renderer dependencies.
// Strength is peak radial acceleration, and the impulse is scaled by dt.
// Radius falloff is smooth; zero radius/strength or disabled state is a no-op.
// The simulation backend remains responsible for final speed limits.
class CursorIntervention : public sim::Intervention {
public:
    void apply(float dt, std::vector<sim::Agent>& agents, sim::World& world) final;

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] glm::vec3 position() const noexcept { return position_; }
    [[nodiscard]] float strength() const noexcept { return strength_; }
    [[nodiscard]] float radius() const noexcept { return radius_; }

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    // Positions must be finite. Radius/strength and apply's dt must be finite
    // and nonnegative; invalid values throw without changing configuration.
    void setPosition(const glm::vec3& position);
    void setStrength(float strength);
    void setRadius(float radius);

protected:
    enum class Direction { Repel, Attract };
    CursorIntervention(float strength, float radius, Direction direction);

private:
    bool enabled_{false};
    glm::vec3 position_{0.0F};
    float strength_;
    float radius_;
    Direction direction_;
};

} // namespace emergences::scenarios::boids
