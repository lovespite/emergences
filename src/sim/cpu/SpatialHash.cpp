#include "sim/cpu/SpatialHash.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace emergences::sim::cpu {

std::size_t SpatialHash::CellHash::operator()(const Cell& cell) const noexcept {
    std::size_t result = 0;
    for (const int coordinate : cell.coordinate) {
        result ^= std::hash<int>{}(coordinate) + std::size_t{0x9e3779b9U} +
                  (result << 6U) + (result >> 2U);
    }
    return result;
}

SpatialHash::Cell SpatialHash::cellFor(const glm::vec3& position) const {
    Cell result;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const double extent = static_cast<double>(world_.maximum[axis]) - world_.minimum[axis];
        const double offset = static_cast<double>(position[axis]) - world_.minimum[axis];
        const double scaled = offset / extent * cellCounts_[axis];
        result.coordinate[axis] = std::clamp(static_cast<int>(scaled), 0, cellCounts_[axis] - 1);
    }
    return result;
}

void SpatialHash::rebuild(std::span<const Agent> agents, const World& world, float maximumRadius) {
    world.validate();
    if (!std::isfinite(maximumRadius) || maximumRadius < 0.0F) {
        throw std::invalid_argument("Spatial hash maximum radius must be finite and nonnegative");
    }
    SpatialHash next;
    next.agents_ = agents;
    next.world_ = world;
    next.maximumRadius_ = maximumRadius;
    // Capping counts prevents tiny radii from overflowing integer coordinates.
    // Larger cells only broaden candidates; exact distance filtering is unchanged.
    constexpr int maximumCellsPerAxis = 1 << 20;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (maximumRadius > 0.0F) {
            const double extent = static_cast<double>(world.maximum[axis]) - world.minimum[axis];
            const double count = std::clamp(std::floor(extent / maximumRadius), 1.0,
                                            static_cast<double>(maximumCellsPerAxis));
            next.cellCounts_[axis] = static_cast<int>(count);
        }
    }
    next.cells_.reserve(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const glm::vec3& position = agents[index].position;
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(position[axis]) || position[axis] < world.minimum[axis] ||
                position[axis] > world.maximum[axis] ||
                (world.boundary == BoundaryMode::Wrap && position[axis] == world.maximum[axis])) {
                throw std::invalid_argument("Spatial hash positions must be finite and inside the world");
            }
        }
        next.cells_[next.cellFor(position)].push_back(index);
    }
    next.built_ = true;
    *this = std::move(next);
}

void SpatialHash::query(std::size_t selfIndex, float radius, std::vector<Neighbor>& output) const {
    output.clear();
    if (!built_) {
        throw std::logic_error("Spatial hash query requires rebuild first");
    }
    if (selfIndex >= agents_.size()) {
        throw std::out_of_range("Spatial hash self index is out of range");
    }
    if (!std::isfinite(radius) || radius < 0.0F || radius > maximumRadius_) {
        throw std::invalid_argument("Spatial hash query radius is outside the rebuild range");
    }
    if (radius == 0.0F) {
        return;
    }

    const Cell center = cellFor(agents_[selfIndex].position);
    std::array<std::array<int, 3>, 3> coordinates{};
    std::array<int, 3> sizes{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (int delta = -1; delta <= 1; ++delta) {
            int coordinate = center.coordinate[axis] + delta;
            if (world_.boundary == BoundaryMode::Wrap) {
                coordinate = (coordinate + cellCounts_[axis]) % cellCounts_[axis];
            } else if (coordinate < 0 || coordinate >= cellCounts_[axis]) {
                continue;
            }
            auto& values = coordinates[axis];
            const auto end = values.begin() + sizes[axis];
            if (std::find(values.begin(), end, coordinate) == end) {
                values[sizes[axis]++] = coordinate;
            }
        }
    }

    const double radiusSquared = static_cast<double>(radius) * radius;
    const glm::dvec3 selfPosition{agents_[selfIndex].position};
    const glm::dvec3 extent = glm::dvec3{world_.maximum} - glm::dvec3{world_.minimum};
    const glm::dvec3 halfExtent = extent * 0.5;
    const bool wrap = world_.boundary == BoundaryMode::Wrap;
    for (int x = 0; x < sizes[0]; ++x) {
        for (int y = 0; y < sizes[1]; ++y) {
            for (int z = 0; z < sizes[2]; ++z) {
                const Cell cell{{coordinates[0][x], coordinates[1][y], coordinates[2][z]}};
                const auto found = cells_.find(cell);
                if (found == cells_.end()) {
                    continue;
                }
                for (const std::size_t index : found->second) {
                    if (index == selfIndex) {
                        continue;
                    }
                    double distanceSquared = 0.0;
                    glm::vec3 displacement{0.0F};
                    for (glm::length_t axis = 0; axis < 3; ++axis) {
                        double delta = static_cast<double>(agents_[index].position[axis]) - selfPosition[axis];
                        if (wrap) {
                            // Rebuild guarantees positions inside one period:
                            // at most one subtraction is needed. Strict ties
                            // preserve the sign at exactly half a period.
                            if (delta > halfExtent[axis]) delta -= extent[axis];
                            else if (delta < -halfExtent[axis]) {
                                delta += extent[axis];
                                // Match remainder's signed zero as well when
                                // extreme world scales round delta to -extent.
                                if (delta == 0.0) delta = -0.0;
                            }
                        }
                        distanceSquared += delta * delta;
                        displacement[axis] = static_cast<float>(delta);
                    }
                    if (distanceSquared <= radiusSquared) {
                        // Filter in double precision; rules reuse the float
                        // displacement without repeating checked world math.
                        output.push_back({&agents_[index], distanceSquared, displacement});
                    }
                }
            }
        }
    }
}

} // namespace emergences::sim::cpu
