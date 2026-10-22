#pragma once

#include "sim/Rule.h"

namespace emergences::scenarios::boids {

class Alignment final : public sim::Rule {
public:
    explicit Alignment(float weight = 1.0F, float radius = 5.0F);

    [[nodiscard]] glm::vec3 steer(const sim::Agent& self,
                                  std::span<const sim::Neighbor> neighbors,
                                  const sim::World& world) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Alignment"; }
    [[nodiscard]] float weight() const noexcept override { return weight_; }
    [[nodiscard]] float radius() const noexcept override { return radius_; }
    void setWeight(float weight) override;
    void setRadius(float radius) override;

private:
    float weight_;
    float radius_;
};

} // namespace emergences::scenarios::boids
