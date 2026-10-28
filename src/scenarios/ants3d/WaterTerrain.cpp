#include "scenarios/ants3d/WaterTerrain.h"

#include "scenarios/ants3d/Terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace emergences::scenarios::ants3d {
namespace {
constexpr float Pi = 3.14159265358979323846F;
constexpr int RiverSegments = 256;
constexpr int PondSegments = 64;
constexpr float BankBlend = 32.0F;

std::uint32_t mixBits(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float randomUnit(std::uint32_t value) noexcept {
    return static_cast<float>(mixBits(value) >> 8U) / 16777216.0F;
}

float smoothRange(float low, float high, float value) noexcept {
    const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

glm::vec2 nearestOnSegment(glm::vec2 point, glm::vec2 a, glm::vec2 b) noexcept {
    const glm::vec2 edge = b - a;
    return a + edge * std::clamp(glm::dot(point - a, edge) / glm::dot(edge, edge), 0.0F, 1.0F);
}

void triangle(std::vector<TerrainVertex>& output, glm::vec3 a, glm::vec3 b, glm::vec3 c,
              glm::vec3 color, glm::vec3 preferredNormal) {
    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    if (glm::dot(normal, preferredNormal) < 0.0F) { std::swap(b, c); normal = -normal; }
    output.push_back({a, normal, color}); output.push_back({b, normal, color}); output.push_back({c, normal, color});
}

void quad(std::vector<TerrainVertex>& output, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          glm::vec3 color, glm::vec3 normal = {0.0F, 1.0F, 0.0F}) {
    triangle(output, a, b, c, color, normal); triangle(output, a, c, d, color, normal);
}

void box(std::vector<TerrainVertex>& output, glm::vec3 low, glm::vec3 high, glm::vec3 color) {
    quad(output, {low.x, high.y, low.z}, {high.x, high.y, low.z}, high, {low.x, high.y, high.z}, color);
    quad(output, low, {high.x, low.y, low.z}, {high.x, low.y, high.z}, {low.x, low.y, high.z}, color * 0.65F, {0,-1,0});
    quad(output, low, {low.x, high.y, low.z}, {high.x, high.y, low.z}, {high.x, low.y, low.z}, color * 0.78F, {0,0,-1});
    quad(output, {low.x, low.y, high.z}, {low.x, high.y, high.z}, high, {high.x, low.y, high.z}, color * 0.84F, {0,0,1});
    quad(output, low, {low.x, low.y, high.z}, {low.x, high.y, high.z}, {low.x, high.y, low.z}, color * 0.70F, {-1,0,0});
    quad(output, {high.x, low.y, low.z}, {high.x, low.y, high.z}, high, {high.x, high.y, low.z}, color * 0.88F, {1,0,0});
}
} // namespace

void WaterTerrain::configure(std::uint32_t seed, float halfExtent) {
    seed_ = seed;
    halfExtent_ = halfExtent;
    riverStep_ = 2.0F * halfExtent_ / RiverSegments;
    const float scale = halfExtent_ / 128.0F;
    const float phase = 2.0F * Pi * randomUnit(seed ^ 0x8da6b343U);
    banks_.resize(RiverSegments + 1);
    for (int row = 0; row <= RiverSegments; ++row) {
        const float z = -halfExtent_ + riverStep_ * row;
        const float center = halfExtent_ * 0.59F + scale *
            (5.0F * std::sin(z * 0.025F / scale + phase) + 2.2F * std::sin(z * 0.057F / scale - phase));
        const float width = scale * (7.0F + 1.0F * std::sin(z * 0.036F / scale + phase * 0.7F));
        banks_[static_cast<std::size_t>(row)] = {center - width, center + width};
    }
    crossings_.clear();
    for (int index = 0; index < 2; ++index) {
        const float z = scale * ((index == 0 ? -45.0F : 45.0F) +
            16.0F * (randomUnit(seed ^ (0x6c8e9cf5U + static_cast<std::uint32_t>(index))) - 0.5F));
        crossings_.push_back({{riverCenter(z), riverLevel_ + 1.35F, z}, 16.0F * scale, 5.0F * scale});
    }
    ponds_.clear();
    pondShorelines_.clear();
    constexpr glm::vec2 Locations[]{{-99.0F, -94.0F}, {-103.0F, 91.0F}, {111.0F, 78.0F}};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto hash = seed ^ (0x68e31da4U + static_cast<std::uint32_t>(index) * 0x9e3779b9U);
        const glm::vec2 offset{6.0F * (randomUnit(hash) - 0.5F), 8.0F * (randomUnit(hash ^ 0x7f4a7c15U) - 0.5F)};
        const glm::vec2 center = scale * (Locations[index] + offset);
        const glm::vec2 radii = scale * glm::vec2{index == 2 ? 6.0F : 9.0F,
            7.0F + 3.0F * randomUnit(hash ^ 0x9e3779b9U)};
        ponds_.push_back({{center.x, 2.5F, center.y}, radii});
        auto& shoreline = pondShorelines_.emplace_back();
        const float pondPhase = randomUnit(hash ^ 0x02e5be93U) * 2.0F * Pi;
        for (int segment = 0; segment < PondSegments; ++segment) {
            const float angle = 2.0F * Pi * segment / PondSegments;
            const float ripple = 1.0F + 0.09F * std::sin(3.0F * angle + pondPhase) +
                                 0.045F * std::sin(5.0F * angle - pondPhase);
            shoreline.push_back(center + radii * glm::vec2{std::cos(angle), std::sin(angle)} * ripple);
        }
    }
}

void WaterTerrain::fitPondLevels(const std::function<float(float, float)>& sampleGround) {
    for (auto& pond : ponds_) pond.center.y = sampleGround(pond.center.x, pond.center.z) - 0.6F;
}

glm::vec2 WaterTerrain::riverEdges(float z) const noexcept {
    if (banks_.empty()) return {0.0F, 0.0F};
    const float row = std::clamp((z + halfExtent_) / riverStep_, 0.0F, static_cast<float>(RiverSegments));
    const int index = std::min(static_cast<int>(row), RiverSegments - 1);
    return glm::mix(banks_[static_cast<std::size_t>(index)], banks_[static_cast<std::size_t>(index + 1)], row - index);
}

float WaterTerrain::riverCenter(float z) const noexcept {
    const auto edges = riverEdges(z); return (edges.x + edges.y) * 0.5F;
}

float WaterTerrain::riverHalfWidth(float z) const noexcept {
    const auto edges = riverEdges(z); return (edges.y - edges.x) * 0.5F;
}

WaterTerrain::ShoreContact WaterTerrain::riverContact(glm::vec2 point) const noexcept {
    ShoreContact result;
    result.clearance = std::numeric_limits<float>::infinity();
    if (banks_.empty()) return result;
    const glm::vec2 edges = riverEdges(point.y);
    const bool inside = point.x > edges.x && point.x < edges.y;
    const int row = std::clamp(static_cast<int>((point.y + halfExtent_) / riverStep_), 0, RiverSegments - 1);
    // Near a bank (where contact matters), its closest segment lies in these
    // neighboring rows. The modest meander slope is bounded below 0.3.
    for (int segment = std::max(0, row - 3); segment <= std::min(RiverSegments - 1, row + 3); ++segment) {
        const float z = -halfExtent_ + segment * riverStep_;
        for (int side = 0; side < 2; ++side) {
            const glm::vec2 a{banks_[static_cast<std::size_t>(segment)][side], z};
            const glm::vec2 b{banks_[static_cast<std::size_t>(segment + 1)][side], z + riverStep_};
            const glm::vec2 nearest = nearestOnSegment(point, a, b);
            const float distance = glm::length(point - nearest);
            if (distance >= std::abs(result.clearance)) continue;
            result.point = nearest;
            result.clearance = inside ? -distance : distance;
            const glm::vec2 edge = b - a;
            result.normal = glm::normalize(glm::vec2{edge.y, -edge.x}) * (side == 0 ? -1.0F : 1.0F);
            if (distance > 1.0e-5F) result.normal = (point - nearest) * ((inside ? -1.0F : 1.0F) / distance);
        }
    }
    return result;
}

WaterTerrain::ShoreContact WaterTerrain::pondContact(std::size_t index, glm::vec2 point) const noexcept {
    const auto& shoreline = pondShorelines_[index];
    ShoreContact result;
    result.clearance = std::numeric_limits<float>::infinity();
    bool inside = false;
    for (std::size_t indexB = 0; indexB < shoreline.size(); ++indexB) {
        const glm::vec2 a = shoreline[indexB];
        const glm::vec2 b = shoreline[(indexB + 1) % shoreline.size()];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
        const glm::vec2 nearest = nearestOnSegment(point, a, b);
        const float distance = glm::length(point - nearest);
        if (distance >= result.clearance) continue;
        result.clearance = distance;
        result.point = nearest;
        const glm::vec2 edge = b - a;
        result.normal = glm::normalize(glm::vec2{edge.y, -edge.x});
    }
    if (result.clearance > 1.0e-5F)
        result.normal = (point - result.point) * ((inside ? -1.0F : 1.0F) / result.clearance);
    if (inside) result.clearance = -result.clearance;
    return result;
}

float WaterTerrain::shapeHeight(float x, float z, float rawHeight) const noexcept {
    if (banks_.empty()) return rawHeight;
    const float riverDistance = std::abs(x - riverCenter(z)) - riverHalfWidth(z);
    float height = rawHeight;
    if (riverDistance < BankBlend) {
        // Keep the visible water's polygon edge at the actual contact shore.
        // A tiny submerged lip hides height-field interpolation seams; the dry
        // bank rises outside it instead of burying a wide strip of the water.
        const float bed = riverLevel_ - 2.2F + 2.12F * smoothRange(-riverHalfWidth(z), 0.0F, riverDistance) +
                          0.48F * smoothRange(0.0F, 1.5F, riverDistance);
        height = std::lerp(bed, rawHeight, smoothRange(0.0F, BankBlend, riverDistance));
    }
    // Dry bridge approaches continue the level deck into both banks and
    // then blend back to the hills over 32 units, with no vertical step.
    for (const auto& bridge : crossings_) {
        const float along = std::abs(x - bridge.center.x);
        const float across = std::abs(z - bridge.center.z);
        const float influence = (1.0F - smoothRange(bridge.halfLength + 2.0F,
            bridge.halfLength + BankBlend, along)) *
            (1.0F - smoothRange(bridge.halfWidth + 1.0F, bridge.halfWidth + 9.0F, across)) *
            smoothRange(0.0F, 2.0F, riverDistance);
        height = std::lerp(height, bridge.center.y, influence);
    }
    for (std::size_t index = 0; index < ponds_.size(); ++index) {
        const auto& pond = ponds_[index];
        const glm::vec2 delta{x - pond.center.x, z - pond.center.z};
        if (std::abs(delta.x) > pond.radii.x * 1.14F + 14.0F ||
            std::abs(delta.y) > pond.radii.y * 1.14F + 14.0F) continue;
        const float distance = pondContact(index, {x, z}).clearance;
        if (distance >= 14.0F) continue;
        const float bed = pond.center.y - 1.7F + 1.62F * smoothRange(-5.0F, 0.0F, distance) +
                          0.48F * smoothRange(0.0F, 1.5F, distance);
        height = std::lerp(bed, height, smoothRange(0.0F, 14.0F, distance));
    }
    return height;
}

bool WaterTerrain::isWater(float x, float z) const noexcept {
    if (!banks_.empty()) {
        const auto edges = riverEdges(z);
        if (x > edges.x && x < edges.y) return true;
    }
    for (std::size_t index = 0; index < ponds_.size(); ++index) {
        const auto& pond = ponds_[index];
        if (std::abs(x - pond.center.x) > pond.radii.x * 1.14F ||
            std::abs(z - pond.center.z) > pond.radii.y * 1.14F) continue;
        if (pondContact(index, {x, z}).clearance < 0.0F) return true;
    }
    return false;
}

bool WaterTerrain::onCrossing(float x, float z, float radius) const noexcept {
    for (const auto& bridge : crossings_) {
        if (std::abs(x - bridge.center.x) <= bridge.halfLength - radius &&
            std::abs(z - bridge.center.z) <= bridge.halfWidth - radius) return true;
    }
    return false;
}

bool WaterTerrain::walkable(float x, float z, float radius) const noexcept {
    radius = std::max(0.0F, radius);
    if (!banks_.empty()) {
        const float distance = std::abs(x - riverCenter(z)) - riverHalfWidth(z);
        if (distance < radius * 1.1F + 0.01F && !onCrossing(x, z, radius) &&
            riverContact({x, z}).clearance < radius) return false;
    }
    for (std::size_t index = 0; index < ponds_.size(); ++index) {
        const auto& pond = ponds_[index];
        if (std::abs(x - pond.center.x) > pond.radii.x * 1.14F + radius ||
            std::abs(z - pond.center.z) > pond.radii.y * 1.14F + radius) continue;
        if (pondContact(index, {x, z}).clearance < radius) return false;
    }
    return true;
}

std::optional<float> WaterTerrain::bridgeHeight(float x, float z) const noexcept {
    for (const auto& bridge : crossings_) {
        if (std::abs(x - bridge.center.x) <= bridge.halfLength &&
            std::abs(z - bridge.center.z) <= bridge.halfWidth) return bridge.center.y;
    }
    return std::nullopt;
}

void WaterTerrain::resolveCollisionXZ(glm::vec3& position, glm::vec3& velocity, float radius) const noexcept {
    constexpr float ContactMargin = 0.002F;
    const float clearance = std::max(0.0F, radius) + ContactMargin;
    const float bound = halfExtent_ - std::max(0.0F, radius);
    auto constrainBounds = [&] {
        for (int axis : {0, 2}) {
            if (position[axis] < -bound) { position[axis] = -bound; velocity[axis] = std::max(0.0F, velocity[axis]); }
            if (position[axis] > bound) { position[axis] = bound; velocity[axis] = std::min(0.0F, velocity[axis]); }
        }
    };
    auto project = [&](glm::vec2 next) {
        const glm::vec2 delta = next - glm::vec2{position.x, position.z};
        const float length = glm::length(delta);
        position.x = next.x; position.z = next.y;
        if (length <= 1.0e-6F) return;
        const glm::vec3 normal{delta.x / length, 0.0F, delta.y / length};
        const float inward = glm::dot(velocity, normal);
        if (inward < 0.0F) velocity -= inward * normal;
    };
    // At the map ends, a curved shore's normal can point out of bounds.
    // Alternate both projections so a later world clamp cannot re-enter water.
    constrainBounds();
    for (int iteration = 0; iteration < 6; ++iteration) {
        if (!banks_.empty() && !onCrossing(position.x, position.z, clearance)) {
            const float distance = std::abs(position.x - riverCenter(position.z)) - riverHalfWidth(position.z);
            if (distance < clearance * 1.1F + 0.01F) {
                const glm::vec2 point{position.x, position.z};
                const auto contact = riverContact(point);
                if (contact.clearance < clearance) {
                    glm::vec2 next = contact.point + contact.normal * clearance;
                    float best = glm::length(next - point);
                    for (const auto& bridge : crossings_) {
                        const glm::vec2 center{bridge.center.x, bridge.center.z};
                        const glm::vec2 extents{bridge.halfLength - clearance, bridge.halfWidth - clearance};
                        if (extents.x <= 0.0F || extents.y <= 0.0F) continue;
                        const glm::vec2 candidate = glm::clamp(point, center - extents, center + extents);
                        const float candidateDistance = glm::length(candidate - point);
                        if (candidateDistance < best) { next = candidate; best = candidateDistance; }
                    }
                    project(next);
                }
            }
        }
        for (std::size_t index = 0; index < ponds_.size(); ++index) {
            const auto& pond = ponds_[index];
            if (std::abs(position.x - pond.center.x) > pond.radii.x * 1.14F + clearance ||
                std::abs(position.z - pond.center.z) > pond.radii.y * 1.14F + clearance) continue;
            const auto contact = pondContact(index, {position.x, position.z});
            if (contact.clearance < clearance) project(contact.point + contact.normal * clearance);
        }
        constrainBounds();
        if (walkable(position.x, position.z, radius)) break;
    }
}

void WaterTerrain::appendMesh(std::vector<TerrainVertex>& water, std::vector<TerrainVertex>& bridges) const {
    const glm::vec3 deep{0.055F, 0.28F, 0.40F}, shallow{0.10F, 0.48F, 0.57F};
    for (int row = 0; row < RiverSegments && !banks_.empty(); ++row) {
        const float z0 = -halfExtent_ + row * riverStep_, z1 = z0 + riverStep_;
        const auto edge0 = banks_[static_cast<std::size_t>(row)], edge1 = banks_[static_cast<std::size_t>(row + 1)];
        constexpr int Strips = 8;
        for (int strip = 0; strip < Strips; ++strip) {
            const float u0 = static_cast<float>(strip) / Strips, u1 = static_cast<float>(strip + 1) / Strips;
            const float shore = std::pow(std::abs(u0 + u1 - 1.0F), 2.0F);
            const float shimmer = 0.018F * std::sin(z0 * 0.5F + strip * 0.6F + randomUnit(seed_) * Pi);
            const auto color = glm::mix(deep, shallow, shore) + glm::vec3{shimmer};
            const float level = riverLevel_ + 0.025F;
            quad(water, {std::lerp(edge0.x, edge0.y, u0), level, z0},
                        {std::lerp(edge0.x, edge0.y, u1), level, z0},
                        {std::lerp(edge1.x, edge1.y, u1), level, z1},
                        {std::lerp(edge1.x, edge1.y, u0), level, z1}, color);
        }
    }
    for (std::size_t index = 0; index < ponds_.size(); ++index) {
        const auto& pond = ponds_[index];
        const auto& shoreline = pondShorelines_[index];
        const glm::vec3 center{pond.center.x, pond.center.y + 0.025F, pond.center.z};
        for (std::size_t segment = 0; segment < shoreline.size(); ++segment) {
            const auto a = shoreline[segment], b = shoreline[(segment + 1) % shoreline.size()];
            const glm::vec3 edgeA{a.x, center.y, a.y}, edgeB{b.x, center.y, b.y};
            const glm::vec3 innerA = glm::mix(center, edgeA, 0.70F), innerB = glm::mix(center, edgeB, 0.70F);
            triangle(water, center, innerA, innerB, deep * glm::vec3{0.95F,1.07F,0.94F}, {0,1,0});
            quad(water, innerA, edgeA, edgeB, innerB, shallow * glm::vec3{0.95F,1.03F,0.90F});
        }
    }
    for (const auto& bridge : crossings_) {
        const auto center = bridge.center;
        const float left = center.x - bridge.halfLength, right = center.x + bridge.halfLength;
        const float back = center.z - bridge.halfWidth, front = center.z + bridge.halfWidth;
        const glm::vec3 wood{0.50F, 0.32F, 0.16F};
        box(bridges, {left, center.y - 0.45F, back}, {right, center.y - 0.07F, front}, wood * 0.7F);
        const int planks = static_cast<int>(std::ceil(bridge.halfLength * 2.0F / 0.85F));
        for (int plank = 0; plank < planks; ++plank) {
            const float x0 = std::lerp(left, right, static_cast<float>(plank) / planks);
            const float x1 = std::lerp(left, right, static_cast<float>(plank + 1) / planks);
            const float shade = 0.90F + 0.15F * randomUnit(seed_ ^ static_cast<std::uint32_t>(plank));
            box(bridges, {x0 + 0.012F, center.y - 0.10F, back}, {x1 - 0.012F, center.y, front}, wood * shade);
        }
        for (const float side : {-1.0F, 1.0F}) {
            const float railZ = center.z + side * (bridge.halfWidth + 0.11F);
            box(bridges, {left, center.y + 0.71F, railZ - 0.06F},
                         {right, center.y + 0.84F, railZ + 0.06F}, wood * 0.80F);
            for (int post = 0; post <= 8; ++post) {
                const float x = std::lerp(left, right, static_cast<float>(post) / 8.0F);
                box(bridges, {x - 0.07F, center.y - 0.20F, railZ - 0.06F},
                             {x + 0.07F, center.y + 0.90F, railZ + 0.06F}, wood * 0.76F);
            }
        }
        for (const float side : {-1.0F, 1.0F}) {
            const float supportX = center.x + side * (riverHalfWidth(center.z) - 1.5F);
            for (const float across : {-0.65F, 0.65F}) {
                const float supportZ = center.z + across * bridge.halfWidth;
                box(bridges, {supportX - 0.34F, riverLevel_ - 1.5F, supportZ - 0.34F},
                             {supportX + 0.34F, center.y - 0.15F, supportZ + 0.34F}, {0.34F,0.32F,0.28F});
            }
        }
    }
}

} // namespace emergences::scenarios::ants3d
