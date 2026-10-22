#pragma once

#include "scenarios/boids/CursorIntervention.h"

namespace emergences::scenarios::boids {

class CursorRepel final : public CursorIntervention {
public:
    explicit CursorRepel(float strength = 30.0F, float radius = 8.0F);
    [[nodiscard]] std::string_view name() const noexcept override { return "CursorRepel"; }
};

} // namespace emergences::scenarios::boids
