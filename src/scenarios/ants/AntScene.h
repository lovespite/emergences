#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include "sim/Agent.h"

namespace emergences::scenarios::ants {

struct AntParameters {
    float speed{6.0F};
    float sensorDistance{3.0F};
    float sensorAngle{0.65F};
    float turnSpeed{4.0F};
    float wander{1.2F};
    float depositRate{4.0F};
    float evaporation{0.18F};
    float diffusion{1.5F};
    float foodSpawnInterval{10.0F};
};

struct FoodSource {
    glm::vec3 position{0.0F};
    float radius{2.0F};
    int remaining{0};
};

struct Enemy {
    glm::vec3 position{0.0F};
    glm::vec3 velocity{0.0F};
    float radius{1.5F};
};

// A deterministic, CPU-only colony on the XZ plane. Ants leave home trails
// while searching and food trails while carrying food; the fields begin empty.
// Borrowed views remain valid until the corresponding state is changed.
class AntScene {
public:
    static constexpr std::size_t MaxAnts = 20000;
    static constexpr std::size_t GridResolution = 256;
    static constexpr std::size_t MaxFoodSources = 24;
    static constexpr std::size_t FoodSourcesPerSpawn = 2;
    static constexpr std::size_t MaxStepsPerAdvance = 8;
    static constexpr double FixedStepSeconds = 1.0 / 120.0;
    static constexpr float WorldHalfExtent = 80.0F;
    static constexpr float MaximumPheromone = 32.0F;

    explicit AntScene(std::size_t count = 1500, std::uint32_t seed = 12345U);

    [[nodiscard]] const AntParameters& parameters() const noexcept;
    // All parameters must be finite. The first eight are nonnegative, with
    // maximums 30, 20, pi, 20, 10, 30, 10, 10. Zero supports a frozen field
    // or stationary ants; the interactive controls may use narrower ranges.
    // The food interval is 1..120 simulated seconds; changing it restarts
    // the countdown without changing existing food, trails, or creatures.
    // Invalid settings are rejected atomically without changing scene state.
    void setParameters(const AntParameters& parameters);
    [[nodiscard]] std::size_t count() const noexcept;
    void setCount(std::size_t count);
    [[nodiscard]] std::uint32_t seed() const noexcept;

    // Regenerate ants, food and enemies with empty trails. These operations
    // retain parameters and pause state, and restart the simulation clock.
    void reset();
    void randomize(std::uint32_t seed);
    void clearPheromones() noexcept;

    // Same fixed-step and optional wall-clock budget semantics as BoidsScene.
    // Nonfinite/negative elapsed time and negative/NaN budgets are rejected,
    // even while paused; positive infinity is an unlimited work budget.
    std::size_t advance(double elapsedSeconds,
                        double workBudgetSeconds = std::numeric_limits<double>::infinity());
    [[nodiscard]] bool lastAdvanceLimited() const noexcept;
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;
    bool singleStep();
    [[nodiscard]] std::uint64_t totalSteps() const noexcept;
    [[nodiscard]] double elapsedSimulationSeconds() const noexcept;

    [[nodiscard]] std::span<const sim::Agent> agents() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> carryingFood() const noexcept;
    [[nodiscard]] std::span<const FoodSource> foodSources() const noexcept;
    [[nodiscard]] std::span<const Enemy> enemies() const noexcept;
    // Grid rows follow +Z, columns follow +X, spanning [-80, 80] inclusively.
    [[nodiscard]] std::span<const float> foodPheromone() const noexcept;
    [[nodiscard]] std::span<const float> homePheromone() const noexcept;
    [[nodiscard]] glm::vec3 nestPosition() const noexcept;
    [[nodiscard]] float nestRadius() const noexcept;
    [[nodiscard]] std::size_t deliveredFood() const noexcept;
    // Includes initial and replenished food; reset restarts this total.
    [[nodiscard]] std::size_t totalGeneratedFood() const noexcept;
    [[nodiscard]] double secondsUntilFoodSpawn() const noexcept;

private:
    void step();
    void updateField(std::vector<float>& field);
    void updateEnemies();
    void spawnFood();
    [[nodiscard]] float sampleField(const std::vector<float>& field,
                                  const glm::vec3& position) const noexcept;
    void deposit(std::vector<float>& field, const glm::vec3& position, float amount);
    [[nodiscard]] float random(float minimum, float maximum);

    AntParameters parameters_;
    std::uint32_t seed_;
    std::mt19937 random_;
    std::vector<sim::Agent> agents_;
    std::vector<std::uint8_t> carryingFood_;
    std::vector<float> headings_;
    std::vector<float> trailStrength_;
    std::vector<FoodSource> foodSources_;
    std::vector<Enemy> enemies_;
    std::vector<float> enemyHeadings_;
    std::vector<float> foodPheromone_;
    std::vector<float> homePheromone_;
    std::vector<float> fieldScratch_;
    double accumulator_{0.0};
    std::uint64_t totalSteps_{0};
    std::size_t deliveredFood_{0};
    std::size_t totalGeneratedFood_{0};
    double nextFoodSpawnTime_{10.0};
    bool paused_{false};
    bool lastAdvanceLimited_{false};
};

} // namespace emergences::scenarios::ants
