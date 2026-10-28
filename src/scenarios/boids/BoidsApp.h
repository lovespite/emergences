#pragma once

#include <filesystem>

namespace emergences::config {
class ConfigFile;
}

namespace emergences::scenarios::boids {

// Scenario composition root, with separate renderer regression fixtures.
class BoidsApp {
public:
    int run(const std::filesystem::path& shaderDirectory, bool smokeTest = false,
            bool gpuSmokeTest = false, config::ConfigFile* config = nullptr,
            bool startAnts = false, bool antsSmokeTest = false,
            bool startParticleLife = false, bool particleLifeSmokeTest = false,
            bool startParticleLife3d = false, bool startAnts3d = false,
            bool ants3dSmokeTest = false, bool startSlimeMold = false, bool slimeMoldSmokeTest = false);
    int runTriangle(const std::filesystem::path& shaderDirectory, bool smokeTest = false);
    int runPreview(const std::filesystem::path& shaderDirectory, bool smokeTest = false,
                   config::ConfigFile* config = nullptr);
};

} // namespace emergences::scenarios::boids
