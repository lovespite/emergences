#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include "sim/Agent.h"
#include "scenarios/ants3d/Terrain.h"
#include "scenarios/ants3d/TerrainNavigation.h"

namespace emergences::scenarios::ants3d {

struct FoodSource {
    glm::vec3 position{0.0F};
    float radius{2.0F};
    int remaining{0};
};

struct TrailPoint {
    glm::vec3 position{0.0F};
    float strength{0.0F};
};

enum class AntActivity { Queued, Leaving, Foraging, Returning, Entering, CoolingDown };

// Independent CPU colony: finite-radius ants climb hills and pass underneath
// the cave roof. Fixed steps, seeded behavior, and an XZ broad phase with 3D
// narrow-phase sphere contacts keep rendering independent of simulation speed.
class Ant3DScene {
public:
    static constexpr std::size_t MaxAnts = 3000;
    static constexpr std::size_t MaxStepsPerAdvance = 8;
    static constexpr float WorldHalfExtent = Terrain::WorldHalfExtent;
    static constexpr float AntRadius = 0.28F;
    static constexpr double FixedStepSeconds = 1.0 / 120.0;
    static constexpr float NestTraversalSeconds = 0.35F;
    static constexpr float DefaultMovementSpeed = 4.7F;
    static constexpr float MinimumMovementSpeed = 0.5F;
    static constexpr float MaximumMovementSpeed = 20.0F;
    static constexpr std::size_t MaximumNestGates = 4;
    static constexpr std::size_t MaxFoodSources = 24;
    // Detection uses 3D distance to the patch center and a clear walking segment.
    static constexpr float FoodDetectionRadius = 10.0F;
    static constexpr float MinimumCooldownSeconds = 5.0F;
    static constexpr float MaximumCooldownSeconds = 30.0F;

    explicit Ant3DScene(std::size_t count = 600, std::uint32_t seed = 12345U);
    [[nodiscard]] std::size_t count() const noexcept;
    void setCount(std::size_t count);
    [[nodiscard]] float movementSpeed() const noexcept;
    void setMovementSpeed(float speed);
    [[nodiscard]] std::size_t nestExitCount() const noexcept;
    void setNestExitCount(std::size_t count);
    [[nodiscard]] std::size_t nestEntryCount() const noexcept;
    void setNestEntryCount(std::size_t count);
    [[nodiscard]] float foodSpawnInterval() const noexcept;
    // Changing the interval restarts its simulation-time countdown.
    void setFoodSpawnInterval(float seconds);
    [[nodiscard]] std::uint32_t seed() const noexcept;
    void setSeed(std::uint32_t seed);
    void randomize(std::uint32_t seed);
    void reset();
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;
    bool singleStep();
    std::size_t advance(double elapsedSeconds,
                        double workBudgetSeconds = std::numeric_limits<double>::infinity());
    [[nodiscard]] std::uint64_t totalSteps() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] double elapsedSimulationSeconds() const noexcept;
    [[nodiscard]] bool lastAdvanceLimited() const noexcept;

    [[nodiscard]] std::span<const sim::Agent> agents() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> carryingFood() const noexcept;
    [[nodiscard]] std::span<const AntActivity> activities() const noexcept;
    [[nodiscard]] bool active(std::size_t index) const noexcept;
    // Most recently completed exit among ants still outside the nest;
    // a new traversal does not qualify until it finishes. Reset clears it.
    [[nodiscard]] std::optional<std::size_t> latestExitedAnt() const noexcept;
    [[nodiscard]] std::span<const float> cooldownRemaining() const noexcept;
    [[nodiscard]] std::span<const FoodSource> foodSources() const noexcept;
    [[nodiscard]] std::span<const TrailPoint> pheromoneTrail() const noexcept;
    [[nodiscard]] glm::vec3 nestPosition() const noexcept;
    [[nodiscard]] glm::vec3 nestExitPosition(std::size_t gate = 0) const noexcept;
    [[nodiscard]] glm::vec3 nestEntryPosition(std::size_t gate = 0) const noexcept;
    [[nodiscard]] float nestRadius() const noexcept;
    [[nodiscard]] std::size_t deliveredFood() const noexcept;
    // Includes the initial patches and all later generated food since reset.
    [[nodiscard]] std::size_t totalGeneratedFood() const noexcept;
    [[nodiscard]] std::size_t collisionContacts() const noexcept;
    [[nodiscard]] const Terrain& terrain() const noexcept;

private:
    void step();
    void resetClock() noexcept;
    void rebuildGrid();
    void resolveAntCollisions();
    void updateNest();
    void spawnFood();
    void updateNavigationDestinations();
    [[nodiscard]] float nextWanderRandom(std::size_t index) noexcept;
    [[nodiscard]] std::size_t nearbyFood(glm::vec3 position) const noexcept;
    [[nodiscard]] glm::vec3 gatePosition(bool outbound, float progress, std::size_t gate = 0) const noexcept;
    [[nodiscard]] glm::vec3 routeTarget(glm::vec3 position, std::size_t index) const noexcept;
    [[nodiscard]] std::size_t cellIndex(glm::vec3 position) const noexcept;

    Terrain terrain_;
    TerrainNavigation navigation_;
    bool navigationReady_{false};
    std::uint32_t navigationSeed_{0};
    glm::vec3 nestPosition_{0.0F};
    std::uint32_t seed_;
    std::mt19937 random_;
    std::vector<sim::Agent> agents_;
    std::vector<std::uint8_t> carryingFood_;
    std::vector<AntActivity> activities_;
    std::vector<float> cooldownRemaining_;
    std::deque<std::size_t> exitQueue_;
    static constexpr std::size_t NoAnt = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t NoFood = std::numeric_limits<std::size_t>::max();
    struct NestGate {
        std::size_t ant{NoAnt};
        float progress{0.0F};
        glm::vec3 approachStart{0.0F};
        float approachLength{0.0F};
    };
    std::vector<NestGate> exitGates_{1};
    std::vector<NestGate> entryGates_{1};
    float movementSpeed_{DefaultMovementSpeed};
    float foodSpawnInterval_{10.0F};
    double foodSpawnElapsed_{0.0};
    std::size_t totalGeneratedFood_{0};
    std::vector<std::uint64_t> exitSequences_;
    std::uint64_t lastExitSequence_{0};
    std::vector<std::size_t> foodTargets_;
    std::vector<std::uint32_t> wanderRandom_;
    std::vector<glm::vec3> explorationHeadings_;
    std::vector<float> explorationRemaining_;
    std::vector<float> wanderPhases_;
    std::vector<glm::vec3> routeTargets_;
    std::vector<FoodSource> foodSources_;
    std::vector<TrailPoint> pheromoneTrail_;
    std::vector<int> gridHeads_;
    std::vector<int> gridNext_;
    std::vector<std::size_t> occupiedCells_;
    double accumulator_{0.0};
    std::uint64_t totalSteps_{0};
    std::uint64_t generation_{0};
    std::size_t deliveredFood_{0};
    std::size_t collisionContacts_{0};
    bool paused_{false};
    bool lastAdvanceLimited_{false};
};

} // namespace emergences::scenarios::ants3d
