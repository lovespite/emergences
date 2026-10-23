#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace emergences::config {

struct BoidsSettings {
    int count = 2048;
    bool gpu = true;
    bool paused = false;
    float alignmentWeight = 0.45F;
    float alignmentRadius = 5.0F;
    float cohesionWeight = 0.11F;
    float cohesionRadius = 6.0F;
    float separationWeight = 2.0F;
    float separationRadius = 2.0F;
    float minSpeed = 1.0F;
    float maxSpeed = 8.0F;
    float maxAcceleration = 20.0F;
    bool bounce = false;
    bool cursor = false;
    bool cursorAttract = false;
    float cursorRadius = 8.0F;
    float cursorStrength = 30.0F;
    float cursorPlaneHeight = 0.0F;
    bool operator==(const BoidsSettings&) const = default;
};

struct AntSettings {
    int count = 1500;
    int seed = 12345;
    bool paused = false;
    float speed = 6.0F;
    float sensorDistance = 3.0F;
    float sensorAngle = 0.65F;
    float turnSpeed = 4.0F;
    float wander = 1.2F;
    float depositRate = 4.0F;
    float evaporation = 0.18F;
    float diffusion = 1.5F;
    bool showHomePheromone = true;
    bool showFoodPheromone = true;
    float agentSize = 0.5F;
    float foodSpawnInterval = 10.0F;
    int obstacleCount = 24;
    bool operator==(const AntSettings&) const = default;
};

struct ViewSettings {
    bool fullscreen = false;
    bool panelVisible = true;
    bool antialiasing = true;
    bool grid = true;
    bool bounds = true;
    int agentShape = 0;
    int colorMode = 0;
    float agentSize = 0.45F;
    bool operator==(const ViewSettings&) const = default;
};

struct CameraSettings {
    float targetX = 0.0F;
    float targetY = 0.0F;
    float targetZ = 0.0F;
    float yaw = 0.7853981633974483F;
    float pitch = 0.5235987755982988F;
    float distance = 220.0F;
    bool operator==(const CameraSettings&) const = default;
};

struct PreviewSettings {
    int count = 2048;
    bool animate = true;
    int colorMode = 0;
    float agentSize = 0.65F;
    bool operator==(const PreviewSettings&) const = default;
};

struct Settings {
    int scenario = 0;
    BoidsSettings boids;
    AntSettings ants;
    ViewSettings view;
    CameraSettings camera;
    PreviewSettings preview;
    bool operator==(const Settings&) const = default;
};

// Reads UTF-8 dotted-key settings and writes changes using an atomic replacement.
// Unknown entries and comments survive saves. I/O failures are reported, not thrown.
class ConfigFile {
public:
    explicit ConfigFile(std::filesystem::path path);

    Settings& settings() noexcept { return settings_; }
    const Settings& settings() const noexcept { return settings_; }
    const std::filesystem::path& path() const noexcept { return path_; }
    const std::vector<std::string>& warnings() const noexcept { return warnings_; }
    const std::string& lastError() const noexcept { return lastError_; }
    bool save();

private:
    void load();

    std::filesystem::path path_;
    Settings settings_;
    Settings savedSettings_;
    std::vector<std::string> lines_;
    std::vector<std::string> warnings_;
    std::string lastError_;
    bool missing_ = true;
    bool readable_ = true;
};

}  // namespace emergences::config
