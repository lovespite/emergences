#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <glm/common.hpp>
#include "scenarios/slime_mold/SlimeMoldScene.h"

namespace emergences::scenarios::slime_mold::model {
inline constexpr float Pi = std::numbers::pi_v<float>;
inline constexpr float Width = 2 * SlimeMoldScene::WorldHalfExtent;
inline constexpr float CellWidth = Width / SlimeMoldScene::GridSize;
inline constexpr float DepositScale = 4096.0F;
inline constexpr float Dt = static_cast<float>(SlimeMoldScene::FixedStepSeconds);

inline void range(float value, float low, float high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::invalid_argument("Slime mold parameter outside its allowed range");
}
inline void validate(const SlimeMoldParameters& p) {
    range(p.speed, 0, 30); range(p.sensorDistance, 0.1F, 15);
    range(p.sensorAngle, 0.01F, Pi); range(p.turnSpeed, 0, 60);
    range(p.wander, 0, 10); range(p.depositRate, 0, 100);
    range(p.diffusion, 0, 60); range(p.evaporation, 0, 10);
    range(p.foodAttraction, 0, 100);
}
// Matching unsigned arithmetic in the shader; random choices depend on identity,
// seed and completed tick, never on workgroup/CPU iteration order.
inline std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16U; x *= 0x7feb352dU; x ^= x >> 15U;
    x *= 0x846ca68bU; x ^= x >> 16U; return x;
}
inline float random(std::uint32_t x) { return static_cast<float>(hash(x) >> 8U) / 16777216.0F; }
inline float wrap(float x) { return x - std::floor((x + 50.0F) / Width) * Width; }
inline std::size_t cell(float x, float z) {
    const auto ix = static_cast<std::uint32_t>(std::floor((wrap(x) + 50.0F) / CellWidth));
    const auto iz = static_cast<std::uint32_t>(std::floor((wrap(z) + 50.0F) / CellWidth));
    return (iz % SlimeMoldScene::GridSize) * SlimeMoldScene::GridSize + ix % SlimeMoldScene::GridSize;
}
inline std::uint32_t deposit(const SlimeMoldParameters& p) {
    return static_cast<std::uint32_t>(std::floor(p.depositRate * Dt * DepositScale + 0.5F));
}
inline sim::Agent fieldParticle(std::size_t i, float trail, float nutrient, float exposure) {
    sim::Agent a;
    a.position = { (static_cast<float>(i % SlimeMoldScene::GridSize) + 0.5F) * CellWidth - 50,
                   0, (static_cast<float>(i / SlimeMoldScene::GridSize) + 0.5F) * CellWidth - 50 };
    a.size = CellWidth;
    const float t = 1.0F - std::exp(-trail * exposure);
    a.color = glm::vec3{0.012F, 0.018F, 0.028F} + glm::vec3{0.75F, 0.90F, 0.45F} * t +
        glm::vec3{0.10F, 0.045F, 0.005F} * nutrient;
    a.color = glm::min(a.color, glm::vec3{1});
    a.id = static_cast<std::uint32_t>(i);
    return a;
}
} // namespace emergences::scenarios::slime_mold::model
