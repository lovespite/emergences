#pragma once

#include "sim/Rule.h"

namespace emergences::scenarios::boids {

class Separation final : public sim::Rule {
public:
    explicit Separation(float weight = 2.0F, float radius = 2.0F);

    [[nodiscard]] glm::vec3 steer(const sim::Agent& self,
                                  std::span<const sim::Neighbor> neighbors,
                                  const sim::World& world) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Separation"; }
    [[nodiscard]] float weight() const noexcept override { return weight_; }
    [[nodiscard]] float radius() const noexcept override { return radius_; }
    void setWeight(float weight) override;
    void setRadius(float radius) override;

private:
    float weight_;
    float radius_;
};

} // namespace emergences::scenarios::boids
