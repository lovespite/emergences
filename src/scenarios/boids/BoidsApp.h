#pragma once

#include <filesystem>

namespace emergences::scenarios::boids {

// Scenario composition root, with separate renderer regression fixtures.
class BoidsApp {
public:
    int run(const std::filesystem::path& shaderDirectory, bool smokeTest = false,
            bool gpuSmokeTest = false);
    int runTriangle(const std::filesystem::path& shaderDirectory, bool smokeTest = false);
    int runPreview(const std::filesystem::path& shaderDirectory, bool smokeTest = false);
};

} // namespace emergences::scenarios::boids
