#pragma once

#include <filesystem>

namespace emergences::scenarios::boids {

// Scenario composition root. Rendering and simulation are added in PLAN.md order.
class BoidsApp {
public:
    int run(const std::filesystem::path& shaderDirectory, bool smokeTest = false);
    int runPreview(const std::filesystem::path& shaderDirectory, bool smokeTest = false);
};

} // namespace emergences::scenarios::boids
