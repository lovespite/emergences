#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "sim/Agent.h"
#include "sim/Rule.h"
#include "sim/World.h"

namespace emergences::sim::cpu {

// Sparse uniform grid. Rebuild borrows agents: their storage and positions must
// remain unchanged until the next rebuild. World is copied. Returned Neighbor
// pointers share that lifetime; results have no defined ordering.
class SpatialHash {
public:
    // Positions must be finite and inside the world's bounds (half-open for
    // wrap, closed for bounce). maximumRadius must be finite and nonnegative.
    // Invalid input throws std::invalid_argument and preserves the old grid.
    void rebuild(std::span<const Agent> agents, const World& world, float maximumRadius);

    // Replaces output. Excludes self by index, includes distances <= radius,
    // and emits no duplicates. A zero radius always returns no neighbors.
    // radius must be finite, nonnegative, and <= the rebuild maximumRadius.
    // Throws std::logic_error before rebuild, std::out_of_range for an invalid
    // self index, and std::invalid_argument for an invalid radius.
    void query(std::size_t selfIndex, float radius, std::vector<Neighbor>& output) const;

private:
    struct Cell {
        std::array<int, 3> coordinate{};
        bool operator==(const Cell&) const = default;
    };

    struct CellHash {
        [[nodiscard]] std::size_t operator()(const Cell& cell) const noexcept;
    };

    [[nodiscard]] Cell cellFor(const glm::vec3& position) const;

    std::span<const Agent> agents_{};
    World world_{};
    std::array<int, 3> cellCounts_{1, 1, 1};
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> cells_;
    float maximumRadius_{0.0F};
    bool built_{false};
};

} // namespace emergences::sim::cpu
