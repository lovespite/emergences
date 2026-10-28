#include "scenarios/ants3d/Terrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace emergences::scenarios::ants3d {
namespace {
constexpr float Pi = 3.14159265358979323846F;


std::uint32_t mixBits(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float unitRandom(std::uint32_t value) noexcept {
    return static_cast<float>(mixBits(value) >> 8U) / 16777216.0F;
}

float fade(float value) noexcept {
    // Perlin's quintic interpolation keeps derivatives continuous at cell edges.
    return value * value * value * (value * (value * 6.0F - 15.0F) + 10.0F);
}

float smoothRange(float low, float high, float value) noexcept {
    const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

float gradient(std::uint32_t seed, int x, int z, float dx, float dz) noexcept {
    const std::uint32_t hash = mixBits(seed ^ mixBits(static_cast<std::uint32_t>(x)) ^
                                     (mixBits(static_cast<std::uint32_t>(z)) * 0x9e3779b9U));
    constexpr float Diagonal = 0.70710678F;
    switch (hash & 7U) {
    case 0: return dx;
    case 1: return -dx;
    case 2: return dz;
    case 3: return -dz;
    case 4: return Diagonal * (dx + dz);
    case 5: return Diagonal * (dx - dz);
    case 6: return Diagonal * (-dx + dz);
    default: return Diagonal * (-dx - dz);
    }
}

float noise(float x, float z, std::uint32_t seed) noexcept {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float dx = x - static_cast<float>(ix), dz = z - static_cast<float>(iz);
    const float a = std::lerp(gradient(seed, ix, iz, dx, dz),
                              gradient(seed, ix + 1, iz, dx - 1.0F, dz), fade(dx));
    const float b = std::lerp(gradient(seed, ix, iz + 1, dx, dz - 1.0F),
                              gradient(seed, ix + 1, iz + 1, dx - 1.0F, dz - 1.0F), fade(dx));
    return 1.41421356F * std::lerp(a, b, fade(dz));
}

float octaves(float x, float z, std::uint32_t seed, int count) noexcept {
    float value = 0.0F, amplitude = 1.0F, total = 0.0F;
    for (int octave = 0; octave < count; ++octave) {
        value += amplitude * noise(x, z, seed + static_cast<std::uint32_t>(octave) * 0x9e3779b9U);
        total += amplitude;
        amplitude *= 0.5F;
        x = x * 2.0F + 17.17F;
        z = z * 2.0F - 9.31F;
    }
    return value / total;
}

// Clip both surfaces at their intersection: the same triangles define the
// visible shell and contacts, including the irregular rims of the entrances.
struct ShellPoint {
    glm::vec3 floor;
    float top;
    float hollow;
};

template<class Visitor>
void visitShellTriangle(const ShellPoint& a, const ShellPoint& b, const ShellPoint& c, Visitor&& visit) {
    if (std::max({a.top, b.top, c.top}) < 0.0001F) return;
    std::array<ShellPoint, 4> clipped;
    int count = 0;
    const std::array<ShellPoint, 3> points{a, b, c};
    ShellPoint previous = c;
    float previousDistance = c.top - c.hollow;
    for (const auto& current : points) {
        const float distance = current.top - current.hollow;
        if ((distance > 0.0F) != (previousDistance > 0.0F)) {
            const float t = previousDistance / (previousDistance - distance);
            clipped[count++] = {glm::mix(previous.floor, current.floor, t),
                                std::lerp(previous.top, current.top, t),
                                std::lerp(previous.hollow, current.hollow, t)};
        }
        if (distance > 0.0F) clipped[count++] = current;
        previous = current;
        previousDistance = distance;
    }
    for (int i = 1; i + 1 < count; ++i) {
        const std::array<ShellPoint, 3> triangle{clipped[0], clipped[i], clipped[i + 1]};
        for (bool upper : {true, false}) {
            std::array<glm::vec3, 3> p;
            float averageHeight = 0.0F;
            for (int j = 0; j < 3; ++j) {
                const float h = upper ? triangle[j].top : triangle[j].hollow;
                p[j] = triangle[j].floor + glm::vec3{0.0F, h, 0.0F};
                averageHeight += h / 3.0F;
            }
            if (glm::length(glm::cross(p[1] - p[0], p[2] - p[0])) > 0.00001F)
                visit(p[0], p[1], p[2], upper, averageHeight);
        }
    }
}

glm::vec3 closestTriangle(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c) noexcept {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) return a;
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) return a + (d1 / (d1 - d3)) * ab;
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) return a + (d2 / (d2 - d6)) * ac;
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && d4 >= d3 && d5 >= d6)
        return b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b);
    const float inverse = 1.0F / (va + vb + vc);
    return a + (vb * inverse) * ab + (vc * inverse) * ac;
}

void removeInto(glm::vec3& velocity, glm::vec3 normal) noexcept {
    const float component = glm::dot(velocity, normal);
    if (component < 0.0F) velocity -= component * normal;
}
} // namespace

Terrain::Terrain(std::uint32_t seed) { regenerate(seed); }

void Terrain::regenerate(std::uint32_t seed) {
    seed_ = seed;
    water_.configure(seed, WorldHalfExtent);
    buildHeightField();
    buildCaves();
    buildMesh();
    featureMesh_.clear();
    water_.appendMesh(featureMesh_, featureMesh_);
}

std::span<const TerrainVertex> Terrain::mesh() const noexcept { return mesh_; }
std::span<const TerrainVertex> Terrain::roofMesh() const noexcept { return roofMesh_; }
std::span<const Cave> Terrain::caves() const noexcept { return caves_; }
std::span<const Cave> Terrain::caveChambers() const noexcept { return chambers_; }
std::span<const glm::vec3> Terrain::caveEntrances() const noexcept { return entrances_; }

void Terrain::buildHeightField() {
    constexpr int Side = GroundResolution + 1;
    constexpr float Spacing = 2.0F * WorldHalfExtent / GroundResolution;
    heights_.resize(Side * Side);
    // Minecraft-inspired independent continentalness, erosion and ridge fields.
    // This is a continuous walkable height field, not Minecraft's voxel generator.
    for (int row = 0; row < Side; ++row) {
        for (int column = 0; column < Side; ++column) {
            const float x = -WorldHalfExtent + Spacing * column;
            const float z = -WorldHalfExtent + Spacing * row;
            const float continental = octaves((x + 91.0F) * 0.006F, (z - 57.0F) * 0.006F,
                                               seed_ ^ 0x68bc21ebU, 4);
            const float erosion = octaves((x - 47.0F) * 0.011F, (z + 113.0F) * 0.011F,
                                          seed_ ^ 0x02e5be93U, 3);
            const float ridge = 1.0F - std::abs(octaves(x * 0.021F, z * 0.021F,
                                                       seed_ ^ 0x967a889bU, 4));
            const float mountains = smoothRange(-0.24F, 0.38F, continental) *
                                    (0.30F + 0.70F * smoothRange(-0.45F, 0.45F, -erosion));
            const float detail = octaves(x * 0.09F, z * 0.09F, seed_ ^ 0x4f1bbcdcU, 3);
            heights_[static_cast<std::size_t>(row * Side + column)] =
                2.0F + 14.0F * continental + 4.0F * erosion +
                mountains * (7.0F + 24.0F * ridge * ridge) + 0.85F * detail;
        }
    }
    // A two-pass Manhattan distance envelope caps adjacent height changes. It
    // prevents rare sharp ridges from creating near-vertical walking normals.
    constexpr float MaxRise = 0.85F * Spacing;
    for (int row = 0; row < Side; ++row) {
        for (int column = 0; column < Side; ++column) {
            auto& height = heights_[static_cast<std::size_t>(row * Side + column)];
            if (column > 0) height = std::min(height, heights_[static_cast<std::size_t>(row * Side + column - 1)] + MaxRise);
            if (row > 0) height = std::min(height, heights_[static_cast<std::size_t>((row - 1) * Side + column)] + MaxRise);
        }
    }
    for (int row = Side - 1; row >= 0; --row) {
        for (int column = Side - 1; column >= 0; --column) {
            auto& height = heights_[static_cast<std::size_t>(row * Side + column)];
            if (column + 1 < Side) height = std::min(height, heights_[static_cast<std::size_t>(row * Side + column + 1)] + MaxRise);
            if (row + 1 < Side) height = std::min(height, heights_[static_cast<std::size_t>((row + 1) * Side + column)] + MaxRise);
        }
    }
    water_.fitPondLevels([&](float x, float z) { return baseHeight(x, z); });
    // Carve after slope limiting so the limiter cannot lower dry approaches
    // below their water level. Both rendering and physics sample this field.
    for (int row = 0; row < Side; ++row) {
        for (int column = 0; column < Side; ++column) {
            auto& height = heights_[static_cast<std::size_t>(row * Side + column)];
            height = water_.shapeHeight(-WorldHalfExtent + Spacing * column,
                                        -WorldHalfExtent + Spacing * row, height);
        }
    }
}

float Terrain::sampleGround(float x, float z, glm::vec3* normal) const noexcept {
    constexpr int Side = GroundResolution + 1;
    constexpr float Spacing = 2.0F * WorldHalfExtent / GroundResolution;
    const float gridX = std::clamp((x + WorldHalfExtent) / Spacing, 0.0F, static_cast<float>(GroundResolution));
    const float gridZ = std::clamp((z + WorldHalfExtent) / Spacing, 0.0F, static_cast<float>(GroundResolution));
    const int column = std::min(static_cast<int>(gridX), GroundResolution - 1);
    const int row = std::min(static_cast<int>(gridZ), GroundResolution - 1);
    const float u = gridX - column, v = gridZ - row;
    const std::size_t index = static_cast<std::size_t>(row * Side + column);
    const float a = heights_[index], b = heights_[index + 1];
    const float c = heights_[index + Side + 1], d = heights_[index + Side];
    // The mesh splits each quad along a--c. Interpolate those same triangles,
    // rather than a bilinear patch that would disagree between mesh vertices.
    const float dx = u >= v ? b - a : c - d;
    const float dz = u >= v ? c - b : d - a;
    if (normal) *normal = glm::normalize(glm::vec3{-dx / Spacing, 1.0F, -dz / Spacing});
    return a + dx * u + dz * v;
}

float Terrain::baseHeight(float x, float z) const noexcept { return sampleGround(x, z); }

glm::vec3 Terrain::groundColor(float x, float z, float height) const noexcept {
    const float temperature = octaves((x + 251.0F) * 0.008F, z * 0.008F, seed_ ^ 0xb5297a4dU, 3);
    const float moisture = octaves(x * 0.01F, (z - 191.0F) * 0.01F, seed_ ^ 0x1b56c4e9U, 3);
    const float dry = smoothRange(0.04F, 0.43F, temperature - moisture);
    const float lush = smoothRange(-0.25F, 0.30F, moisture);
    glm::vec3 color = glm::mix(glm::vec3{0.40F, 0.48F, 0.24F}, glm::vec3{0.17F, 0.34F, 0.19F}, lush);
    color = glm::mix(color, glm::vec3{0.68F, 0.58F, 0.35F}, dry);
    glm::vec3 normal;
    (void)sampleGround(x, z, &normal);
    const float rock = smoothRange(0.12F, 0.32F, 1.0F - normal.y) * 0.65F +
                       0.35F * smoothRange(16.0F, 25.0F, height);
    color = glm::mix(color, glm::vec3{0.46F, 0.45F, 0.40F}, rock);
    const float snow = smoothRange(20.0F, 28.0F, height - 8.0F * temperature);
    color = glm::mix(color, glm::vec3{0.84F, 0.88F, 0.84F}, snow);
    const float shade = 0.025F * noise(x * 0.38F, z * 0.38F, seed_ ^ 0x7f4a7c15U);
    return color + glm::vec3{shade};
}

void Terrain::buildCaves() {
    constexpr float FootprintHalf = CaveResolution * CaveSpacing * 0.5F;
    const auto dryRectangle = [&](float left, float right, float bottom, float top) {
        constexpr float Clearance = 1.0F;
        if (left < -WorldHalfExtent + Clearance || right > WorldHalfExtent - Clearance ||
            bottom < -WorldHalfExtent + Clearance || top > WorldHalfExtent - Clearance) return false;
        // Cave candidates occupy the broad west bank. Check every river-bank
        // segment so even a meander between the corners cannot cross the shell.
        for (float z = bottom; z <= top; z += 1.0F)
            if (right + Clearance >= water_.riverCenter(z) - water_.riverHalfWidth(z)) return false;
        for (const auto& bridge : water_.crossings()) {
            if (right + Clearance >= bridge.center.x - bridge.halfLength &&
                left - Clearance <= bridge.center.x + bridge.halfLength &&
                top + Clearance >= bridge.center.z - bridge.halfWidth &&
                bottom - Clearance <= bridge.center.z + bridge.halfWidth) return false;
        }
        for (const auto& pond : water_.ponds()) {
            // Shoreline ripples reach at most 1.135 times each nominal radius.
            // Conservative bounds leave dry approaches around the whole cave.
            const auto radius = pond.radii * 1.14F + glm::vec2{Clearance};
            if (right >= pond.center.x - radius.x && left <= pond.center.x + radius.x &&
                top >= pond.center.z - radius.y && bottom <= pond.center.z + radius.y) return false;
        }
        return true;
    };
    float cx = 0.0F, cz = 0.0F;
    // A separate hash stream makes placement reproducible without consuming
    // the colony's exploration/randomization sequence. The central fallback is
    // dry for the river and pond layout, even if every bounded proposal fails.
    for (std::uint32_t attempt = 0; attempt < 128; ++attempt) {
        const auto candidateSeed = seed_ ^ mixBits(attempt + 0x9e3779b9U);
        const float x = std::round(-65.0F + 100.0F * unitRandom(candidateSeed ^ 0xc2b2ae35U));
        const float z = std::round(-75.0F + 150.0F * unitRandom(candidateSeed ^ 0x27d4eb2fU));
        if (!dryRectangle(x - FootprintHalf, x + FootprintHalf, z - FootprintHalf, z + FootprintHalf) ||
            !dryRectangle(x - 52.0F, x - 30.0F, z - 14.0F, z + 10.0F)) continue;
        cx = x; cz = z;
        break;
    }
    caves_ = {{{cx, baseHeight(cx, cz), cz}, 32.0F, 31.0F, 7.4F}};
    chambers_.clear();
    const std::array<Cave, 4> rooms{{
        {{0, 0, 0}, 8.5F, 7.0F, 6.8F}, {{12, 0, 4}, 7.5F, 6.0F, 5.8F},
        {{-5, 0, 13}, 6.0F, 6.0F, 6.5F}, {{-10, 0, -8}, 5.5F, 5.0F, 5.0F}
    }};
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        Cave room = rooms[i];
        const float variation = 0.92F + 0.16F * unitRandom(seed_ ^ (0xa341316cU + static_cast<std::uint32_t>(i)));
        room.halfLength *= variation;
        room.halfWidth *= variation;
        room.height *= variation;
        room.center += glm::vec3{cx, 0.0F, cz};
        room.center.y = baseHeight(room.center.x, room.center.z);
        chambers_.push_back(room);
    }
    const std::array<glm::vec2, 10> mainPath{{
        {-34, -5}, {-26, -4}, {-18, -1}, {-11, 2}, {-5, -1},
        {0, 0}, {7, 3}, {14, 4}, {24, 1}, {34, 5}
    }};
    const std::array<glm::vec2, 6> sidePath{{{0, 0}, {-3, 5}, {-2, 9}, {-5, 13}, {-5, 23}, {-10, 34}}};
    const std::array<glm::vec2, 4> alcovePath{{{-5, -1}, {-6, -4}, {-9, -6}, {-10, -8}}};
    constexpr int Side = CaveResolution + 1;
    constexpr float Half = CaveResolution * CaveSpacing * 0.5F;
    moundHeights_.resize(Side * Side);
    cavityHeights_.resize(Side * Side);
    auto passage = [](glm::vec2 point, auto& path, float width, float height) {
        float result = 0.0F;
        for (std::size_t i = 1; i < path.size(); ++i) {
            const glm::vec2 edge = path[i] - path[i - 1];
            const float t = std::clamp(glm::dot(point - path[i - 1], edge) / glm::dot(edge, edge), 0.0F, 1.0F);
            const float distance = glm::length(point - path[i - 1] - t * edge);
            const float localWidth = width * (1.0F + 0.11F * std::sin(static_cast<float>(i) * 1.7F + t * 2.0F));
            const float radial = distance / localWidth;
            if (radial < 1.0F) result = std::max(result, height * std::sqrt(1.0F - radial * radial));
        }
        return result;
    };
    for (int row = 0; row < Side; ++row) {
        for (int column = 0; column < Side; ++column) {
            const float x = -Half + column * CaveSpacing, z = -Half + row * CaveSpacing;
            const float warpX = 0.48F * noise(x * 0.24F + 11.0F, z * 0.24F, seed_ ^ 0x2545f491U);
            const float warpZ = 0.48F * noise(x * 0.24F, z * 0.24F + 47.0F, seed_ ^ 0xa3c59ac3U);
            const glm::vec2 p{x + warpX, z + warpZ};
            float hollow = std::max({passage(p, mainPath, 3.1F, 3.8F),
                                     passage(p, sidePath, 2.8F, 3.6F),
                                     passage(p, alcovePath, 2.6F, 3.4F)});
            for (const auto& room : chambers_) {
                const float dx = (p.x - room.center.x + cx) / room.halfLength;
                const float dz = (p.y - room.center.z + cz) / room.halfWidth;
                const float radial = dx * dx + dz * dz;
                if (radial < 1.0F) hollow = std::max(hollow, room.height * std::sqrt(1.0F - radial));
            }
            if (hollow > 0.0F) hollow *= 1.0F + 0.075F * noise(x * 0.48F, z * 0.48F, seed_ ^ 0x91e10da5U);
            // A broad, asymmetrical hill, with lobes above the side chambers.
            // Its smooth outer slopes meet the unchanged terrain at every edge.
            const float px = x + 0.85F * noise(x * 0.13F, z * 0.13F, seed_ ^ 0x7f4a7c15U);
            const float pz = z + 0.85F * noise(x * 0.13F + 17.0F, z * 0.13F, seed_ ^ 0x635d2dffU);
            const float r = std::sqrt(px * px / (30.0F * 30.0F) + pz * pz / (29.0F * 29.0F));
            const float envelope = r < 1.0F ? 0.5F + 0.5F * std::cos(Pi * r) : 0.0F;
            const float northLobe = std::max(0.0F, 1.0F - ((x + 5.0F) * (x + 5.0F) / 256.0F + (z - 12.0F) * (z - 12.0F) / 196.0F));
            const float eastLobe = std::max(0.0F, 1.0F - ((x - 12.0F) * (x - 12.0F) / 225.0F + (z - 4.0F) * (z - 4.0F) / 196.0F));
            const float mound = envelope * (12.5F + 1.0F * octaves(x * 0.15F, z * 0.15F, seed_ ^ 0xb7e15162U, 3)) +
                                4.8F * northLobe * northLobe + 2.2F * eastLobe * eastLobe;
            const std::size_t index = static_cast<std::size_t>(row * Side + column);
            moundHeights_[index] = mound;
            cavityHeights_[index] = hollow;
        }
    }
    entrances_.clear();
    auto portal = [&](auto& path, bool reverse) {
        glm::vec3 opening;
        for (std::size_t segment = 1; segment < path.size(); ++segment) {
            const glm::vec2 outside = reverse ? path[path.size() - segment] : path[segment - 1];
            const glm::vec2 inside = reverse ? path[path.size() - segment - 1] : path[segment];
            for (int step = 0; step <= 80; ++step) {
                const glm::vec2 p = glm::mix(outside, inside, static_cast<float>(step) / 80.0F);
                if (sampleCave(cx + p.x, cz + p.y, true) > sampleCave(cx + p.x, cz + p.y, false)) {
                    entrances_.push_back(opening);
                    return;
                }
                glm::vec3 body{cx + p.x, supportHeight(cx + p.x, cz + p.y, 0.35F, false), cz + p.y};
                if (sphereClear(body, 0.35F)) opening = {body.x, baseHeight(body.x, body.z), body.z};
            }
        }
        entrances_.push_back(opening);
    };
    portal(mainPath, false);
    portal(mainPath, true);
    portal(sidePath, true);
}

float Terrain::sampleCave(float x, float z, bool upper, glm::vec3* normal) const noexcept {
    constexpr int Side = CaveResolution + 1;
    constexpr float Half = CaveResolution * CaveSpacing * 0.5F;
    const float gx = (x - caves_[0].center.x + Half) / CaveSpacing;
    const float gz = (z - caves_[0].center.z + Half) / CaveSpacing;
    if (gx < 0.0F || gz < 0.0F || gx >= CaveResolution || gz >= CaveResolution) {
        if (normal) *normal = {0.0F, 1.0F, 0.0F};
        return 0.0F;
    }
    const int column = static_cast<int>(gx), row = static_cast<int>(gz);
    const float u = gx - column, v = gz - row;
    const auto& values = upper ? moundHeights_ : cavityHeights_;
    const std::size_t index = static_cast<std::size_t>(row * Side + column);
    const float a = values[index], b = values[index + 1], c = values[index + Side + 1], d = values[index + Side];
    const float dx = u >= v ? b - a : c - d;
    const float dz = u >= v ? c - b : d - a;
    if (normal) {
        glm::vec3 ground;
        (void)sampleGround(x, z, &ground);
        *normal = glm::normalize(glm::vec3{ground.x / ground.y - dx / CaveSpacing, 1.0F,
                                          ground.z / ground.y - dz / CaveSpacing});
    }
    return a + dx * u + dz * v;
}

float Terrain::surfaceHeight(float x, float z) const noexcept {
    if (const auto bridge = water_.bridgeHeight(x, z)) return *bridge;
    const float mound = sampleCave(x, z, true);
    // At an entrance the carved volume protrudes through the hill. Those
    // clipped roof triangles do not provide a walking surface.
    return baseHeight(x, z) + (mound > sampleCave(x, z, false) ? mound : 0.0F);
}
float Terrain::ceilingHeight(float x, float z) const noexcept { return baseHeight(x, z) + sampleCave(x, z, false); }

float Terrain::navigationHeight(float x, float z) const noexcept {
    if (const auto bridge = water_.bridgeHeight(x, z)) return *bridge;
    return sampleCave(x, z, false) > 0.0F ? baseHeight(x, z) : surfaceHeight(x, z);
}

bool Terrain::navigationClear(glm::vec3 bodyCenter, float radius) const noexcept { return sphereClear(bodyCenter, radius); }

glm::vec3 Terrain::normalAt(float x, float z, bool upper) const noexcept {
    glm::vec3 normal;
    if (upper && sampleCave(x, z, true, &normal) > sampleCave(x, z, false)) return normal;
    (void)sampleGround(x, z, &normal);
    return normal;
}

float Terrain::supportHeight(float x, float z, float radius, bool upper) const noexcept {
    float support = -std::numeric_limits<float>::infinity();
    // Adjacent mesh planes also support a sphere straddling a triangulation
    // seam. Using all five samples keeps the body clear on terrain ridges.
    for (const glm::vec2 offset : {glm::vec2{0, 0}, glm::vec2{-radius, -radius},
                                 glm::vec2{radius, -radius}, glm::vec2{-radius, radius}, glm::vec2{radius, radius}}) {
        const float sx = x + offset.x, sz = z + offset.y;
        const glm::vec3 normal = normalAt(sx, sz, upper);
        const float height = upper ? surfaceHeight(sx, sz) : baseHeight(sx, sz);
        const float atCenter = height + (normal.x * offset.x + normal.z * offset.y) / normal.y;
        support = std::max(support, atCenter + radius / std::max(0.15F, normal.y));
    }
    return support;
}

bool Terrain::insideCave(glm::vec3 position) const noexcept {
    const float floor = baseHeight(position.x, position.z);
    const float hollow = sampleCave(position.x, position.z, false);
    return hollow > 0.4F && sampleCave(position.x, position.z, true) > hollow &&
           position.y >= floor - 0.01F && position.y < floor + hollow;
}

glm::vec3 Terrain::surfaceNormal(glm::vec3 position) const noexcept {
    if (water_.bridgeHeight(position.x, position.z)) return {0, 1, 0};
    return normalAt(position.x, position.z, position.y > surfaceHeight(position.x, position.z) - 0.5F);
}

float Terrain::walkingHeight(glm::vec3 position, float radius) const noexcept {
    if (const auto bridge = water_.bridgeHeight(position.x, position.z)) return *bridge + radius;
    const float mound = sampleCave(position.x, position.z, true);
    const bool upper = mound > sampleCave(position.x, position.z, false) &&
                       position.y >= baseHeight(position.x, position.z) + mound - 2.0F * radius;
    const float floor = supportHeight(position.x, position.z, radius, false);
    if (!upper) return floor;
    position.y = std::max(floor, supportHeight(position.x, position.z, radius, true));
    // Clipped portal edges can support a body above its local height plane.
    // Match physical grounding so navigation does not misclassify these rims.
    for (int iteration = 0; iteration < 8; ++iteration) {
        glm::vec3 outward;
        const float distance = rockClearance(position, radius, outward);
        if (distance >= radius - 0.001F) break;
        position.y += (radius + 0.004F - distance) / std::max(0.2F, outward.y);
    }
    return position.y;
}

float Terrain::rockClearance(glm::vec3 position, float radius, glm::vec3& outward) const noexcept {
    constexpr int Side = CaveResolution + 1;
    constexpr float Half = CaveResolution * CaveSpacing * 0.5F;
    const glm::vec3 center = caves_[0].center;
    const float localX = position.x - center.x, localZ = position.z - center.z;
    if (std::abs(localX) > 31.5F + radius || std::abs(localZ) > 30.5F + radius) return std::numeric_limits<float>::infinity();
    const float floor = baseHeight(position.x, position.z);
    const float mound = sampleCave(position.x, position.z, true);
    const float hollow = sampleCave(position.x, position.z, false);
    const bool inRock = mound > hollow && position.y < floor + mound && position.y > floor + hollow;
    const int minX = std::max(0, static_cast<int>(std::floor((localX + Half - radius) / CaveSpacing)));
    const int maxX = std::min(CaveResolution - 1, static_cast<int>(std::floor((localX + Half + radius) / CaveSpacing)));
    const int minZ = std::max(0, static_cast<int>(std::floor((localZ + Half - radius) / CaveSpacing)));
    const int maxZ = std::min(CaveResolution - 1, static_cast<int>(std::floor((localZ + Half + radius) / CaveSpacing)));
    float nearestSquared = std::numeric_limits<float>::infinity();
    outward = {0, 1, 0};
    for (int row = minZ; row <= maxZ; ++row) {
        for (int column = minX; column <= maxX; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * Side + column);
            auto point = [&](int dx, int dz) {
                const std::size_t i = index + static_cast<std::size_t>(dx + dz * Side);
                const float x = center.x - Half + (column + dx) * CaveSpacing;
                const float z = center.z - Half + (row + dz) * CaveSpacing;
                return ShellPoint{{x, baseHeight(x, z), z}, moundHeights_[i], cavityHeights_[i]};
            };
            const auto a = point(0, 0), b = point(1, 0), c = point(1, 1), d = point(0, 1);
            auto contact = [&](glm::vec3 p, glm::vec3 q, glm::vec3 r, bool upper, float) {
                const glm::vec3 delta = position - closestTriangle(position, p, q, r);
                const float squared = glm::dot(delta, delta);
                if (squared >= nearestSquared) return;
                nearestSquared = squared;
                if (squared > 0.0000001F) outward = glm::normalize(delta) * (inRock ? -1.0F : 1.0F);
                else {
                    outward = glm::normalize(glm::cross(q - p, r - p));
                    if ((outward.y > 0.0F) != upper) outward = -outward;
                }
            };
            visitShellTriangle(a, b, c, contact);
            visitShellTriangle(a, c, d, contact);
        }
    }
    return (inRock ? -1.0F : 1.0F) * std::sqrt(nearestSquared);
}

bool Terrain::sphereClear(glm::vec3 position, float radius, float tolerance) const noexcept {
    const float bound = WorldHalfExtent - radius + tolerance;
    if (std::abs(position.x) > bound || std::abs(position.z) > bound) return false;
    if (!water_.walkable(position.x, position.z, std::max(0.0F, radius - tolerance))) return false;
    if (const auto bridge = water_.bridgeHeight(position.x, position.z))
        return position.y >= *bridge + radius - tolerance;
    if (position.y < supportHeight(position.x, position.z, radius, false) - tolerance) return false;
    glm::vec3 outward;
    return rockClearance(position, radius, outward) >= radius - tolerance;
}

void Terrain::resolveCollision(glm::vec3& position, glm::vec3& velocity, float radius) const noexcept {
    water_.resolveCollisionXZ(position, velocity, radius);
    const float bound = WorldHalfExtent - radius;
    for (int axis : {0, 2}) {
        if (position[axis] < -bound) { position[axis] = -bound; velocity[axis] = std::max(0.0F, velocity[axis]); }
        if (position[axis] > bound) { position[axis] = bound; velocity[axis] = std::min(0.0F, velocity[axis]); }
    }
    if (const auto bridge = water_.bridgeHeight(position.x, position.z)) {
        position.y = *bridge + radius;
        removeInto(velocity, {0, 1, 0});
        return;
    }
    const float mound = sampleCave(position.x, position.z, true);
    const bool upper = mound > sampleCave(position.x, position.z, false) &&
                       position.y >= baseHeight(position.x, position.z) + mound - 2.0F * radius;
    position.y = walkingHeight(position, radius);
    glm::vec3 outward;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const float distance = rockClearance(position, radius, outward);
        if (distance >= radius - 0.001F) {
            removeInto(velocity, normalAt(position.x, position.z, upper));
            return;
        }
        if (upper) {
            position.y += (radius + 0.004F - distance) / std::max(0.2F, outward.y);
            removeInto(velocity, outward);
            continue;
        }
        // Retain the lower walking layer. The horizontal projection prevents a
        // cave wall from teleporting an ant onto the hill above it.
        const glm::vec3 ground = normalAt(position.x, position.z, false);
        glm::vec2 horizontal{outward.x - outward.y * ground.x / ground.y,
                             outward.z - outward.y * ground.z / ground.y};
        const float magnitude = glm::length(horizontal);
        if (magnitude < 0.05F || !std::isfinite(distance)) break;
        horizontal /= magnitude;
        const float correction = std::clamp((radius + 0.004F - distance) / magnitude, 0.015F, 1.0F);
        position.x += horizontal.x * correction;
        position.z += horizontal.y * correction;
        position.y = supportHeight(position.x, position.z, radius, false);
        removeInto(velocity, {horizontal.x, 0, horizontal.y});
    }
    if (sphereClear(position, radius)) return;
    // Recovery for externally placed bodies inside solid rock. Routine walking
    // contacts use the local projection above, so this search is rarely needed.
    const glm::vec3 origin = position;
    for (float distance = 0.5F; distance <= 42.0F; distance += 0.5F) {
        const int samples = std::max(16, static_cast<int>(distance * 12.0F));
        for (int i = 0; i < samples; ++i) {
            const float angle = 2.0F * Pi * static_cast<float>(i) / samples;
            glm::vec3 candidate{origin.x + distance * std::cos(angle), 0, origin.z + distance * std::sin(angle)};
            candidate.y = supportHeight(candidate.x, candidate.z, radius, false);
            if (!sphereClear(candidate, radius)) continue;
            position = candidate;
            velocity = {0, 0, 0};
            return;
        }
    }
    position.y = std::max(supportHeight(position.x, position.z, radius, false),
                          supportHeight(position.x, position.z, radius, true));
    velocity = {0, 0, 0};
}

void Terrain::buildMesh() {
    mesh_.clear();
    roofMesh_.clear();
    mesh_.reserve(static_cast<std::size_t>(GroundResolution * GroundResolution * 6 + 100000));
    roofMesh_.reserve(150000);
    auto triangle = [](std::vector<TerrainVertex>& output, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                       glm::vec3 color, bool upper) {
        glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        if ((normal.y > 0.0F) != upper) { std::swap(b, c); normal = -normal; }
        output.push_back({a, normal, color}); output.push_back({b, normal, color}); output.push_back({c, normal, color});
    };
    for (int row = 0; row < GroundResolution; ++row) {
        for (int column = 0; column < GroundResolution; ++column) {
            const float x = -WorldHalfExtent + 2.0F * WorldHalfExtent * column / GroundResolution;
            const float z = -WorldHalfExtent + 2.0F * WorldHalfExtent * row / GroundResolution;
            const float nextX = x + 2.0F * WorldHalfExtent / GroundResolution;
            const float nextZ = z + 2.0F * WorldHalfExtent / GroundResolution;
            const float height = baseHeight(x, z);
            glm::vec3 color = groundColor(x + 0.5F, z + 0.5F, height);
            const float hollow = sampleCave(x + 0.5F, z + 0.5F, false);
            if (hollow > 0.0F && sampleCave(x + 0.5F, z + 0.5F, true) > hollow)
                color = glm::mix(glm::vec3{0.30F, 0.25F, 0.18F}, glm::vec3{0.42F, 0.35F, 0.24F}, smoothRange(0.0F, 5.0F, hollow));
            const glm::vec3 a{x, height, z}, b{nextX, baseHeight(nextX, z), z};
            const glm::vec3 c{nextX, baseHeight(nextX, nextZ), nextZ}, d{x, baseHeight(x, nextZ), nextZ};
            triangle(mesh_, a, b, c, color, true);
            triangle(mesh_, a, c, d, color, true);
        }
    }
    constexpr int Side = CaveResolution + 1;
    constexpr float Half = CaveResolution * CaveSpacing * 0.5F;
    const glm::vec3 center = caves_[0].center;
    for (int row = 0; row < CaveResolution; ++row) {
        for (int column = 0; column < CaveResolution; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * Side + column);
            auto point = [&](int dx, int dz) {
                const std::size_t i = index + static_cast<std::size_t>(dx + dz * Side);
                const float x = center.x - Half + (column + dx) * CaveSpacing;
                const float z = center.z - Half + (row + dz) * CaveSpacing;
                return ShellPoint{{x, baseHeight(x, z), z}, moundHeights_[i], cavityHeights_[i]};
            };
            const auto a = point(0, 0), b = point(1, 0), c = point(1, 1), d = point(0, 1);
            auto shell = [&](glm::vec3 p, glm::vec3 q, glm::vec3 r, bool upper, float height) {
                if (!upper && height < 0.015F) return;
                const float variation = 0.035F * noise((p.x + q.x + r.x) * 0.19F, (p.z + q.z + r.z) * 0.19F, seed_);
                glm::vec3 color = (upper ? glm::vec3{0.43F, 0.40F, 0.31F} : glm::vec3{0.32F, 0.28F, 0.22F}) + glm::vec3{variation};
                if (upper) color = glm::mix(color, glm::vec3{0.29F, 0.39F, 0.23F}, 0.45F * smoothRange(3.0F, 9.0F, height));
                auto& output = (upper ? height > 2.0F : height > 1.4F) ? roofMesh_ : mesh_;
                triangle(output, p, q, r, color, upper);
            };
            visitShellTriangle(a, b, c, shell);
            visitShellTriangle(a, c, d, shell);
        }
    }
}

} // namespace emergences::scenarios::ants3d
