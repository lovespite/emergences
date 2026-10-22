#pragma once

#include "scenarios/boids/CursorIntervention.h"

namespace emergences::scenarios::boids {

class CursorAttract final : public CursorIntervention {
public:
    explicit CursorAttract(float strength = 30.0F, float radius = 8.0F);
    [[nodiscard]] std::string_view name() const noexcept override { return "CursorAttract"; }
};

} // namespace emergences::scenarios::boids
