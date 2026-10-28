#include "config/AppConfig.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using emergences::config::ConfigFile;
using emergences::config::Settings;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct ScratchDirectory {
    std::filesystem::path path = std::filesystem::current_path() /
        ("config-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ScratchDirectory() {
        require(std::filesystem::create_directory(path), "Must create unique test directory");
    }
    ~ScratchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    require(!output.fail(), "Test fixture write failed");
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "Test fixture read failed");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void missingFileCreatesDefaults(const std::filesystem::path& directory) {
    const auto path = directory / std::filesystem::path(u8"配置.cfg");
    ConfigFile config(path);
    require(config.path() == path, "Config must retain Unicode path");
    require(config.settings() == Settings{}, "Missing config uses defaults");
    require(config.warnings().empty() && config.lastError().empty(), "Missing config is normal");
    require(!std::filesystem::exists(path), "Loading must not create a config");
    require(config.save(), "First save must create config");
    const auto text = read(path);
    require(text.find("scenarios.boids.cohesion_weight = 0.11\n") != std::string::npos,
            "Floating point values must remain readable");
    require(text.find("view.agent_shape = arrow\n") != std::string::npos,
            "Enums must use symbolic names");
    require(text.find("app.scenario = boids\n") != std::string::npos &&
                text.find("scenarios.ants.seed = 12345\n") != std::string::npos &&
                text.find("scenarios.ants.obstacle_count = 24\n") != std::string::npos &&
                text.find("scenarios.ants.food_spawn_interval = 10\n") != std::string::npos,
            "Existing users must retain Boids while new ant settings receive defaults");
    require(text.find("view.fullscreen = off\n") != std::string::npos,
            "Booleans must use on/off");
    require(text.find("scenarios.ants3d.count = 600\n") != std::string::npos &&
                text.find("scenarios.ants3d.seed = 12345\n") != std::string::npos &&
                text.find("scenarios.ants3d.paused = off\n") != std::string::npos &&
                text.find("scenarios.ants3d.movement_speed = 4.7\n") != std::string::npos &&
                text.find("scenarios.ants3d.nest_exit_count = 1\n") != std::string::npos &&
                text.find("scenarios.ants3d.nest_entry_count = 1\n") != std::string::npos &&
                text.find("scenarios.ants3d.food_spawn_interval = 10\n") != std::string::npos &&
                text.find("scenarios.ants3d.show_colliders = off\n") != std::string::npos &&
                text.find("scenarios.ants3d.show_cave_roofs = on\n") != std::string::npos &&
                text.find("scenarios.ants3d.show_trails = on\n") != std::string::npos,
            "3D ant settings must receive independent defaults");
    require(text.find("scenarios.particle_life.count = 2000\n") != std::string::npos &&
                text.find("scenarios.particle_life.backend = gpu\n") != std::string::npos &&
                text.find("scenarios.particle_life.dimensions = 2d\n") != std::string::npos &&
                text.find("scenarios.particle_life.species_count = 6\n") != std::string::npos &&
                text.find("scenarios.particle_life.custom_matrix = off\n") != std::string::npos &&
                text.find("scenarios.particle_life.matrix.7.7 = 0\n") != std::string::npos,
            "Particle Life defaults must preserve generated rules and all matrix entries");
    ConfigFile reloaded(path);
    require(reloaded.settings() == config.settings() && reloaded.warnings().empty(),
            "Saved defaults must round trip without warnings");
    const auto oldTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    std::filesystem::last_write_time(path, oldTime);
    const auto recordedTime = std::filesystem::last_write_time(path);
    require(config.save(), "Unchanged save must succeed");
    require(std::filesystem::last_write_time(path) == recordedTime,
            "Unchanged saves must not touch disk");
}

void examplesAndOrder(const std::filesystem::path& directory) {
    const auto path = directory / "examples.cfg";
    write(path, "\xEF\xBB\xBF# UTF-8 BOM and CRLF\r\n"
                "  scenarios.boids.cohesion_weight = 0.11 # flocking\r\n"
                "scenarios.boids.min_speed = 8\r\n"
                "scenarios.boids.max_speed = 14\r\n"
                "view.fullscreen = on\r\n"
                "view.agent_shape = arrow\r\n"
                "view.grid = false ; boolean alias\r\n"
                "view.bounds = 0\r\n"
                "view.panel_visible = 1\r\n"
                "scenarios.boids.cursor.enabled = true\r\n"
                "scenarios.boids.backend = cpu\r\n"
                "scenarios.boids.boundary = bounce\r\n"
                "scenarios.boids.cursor.mode = attract\r\n"
                "view.camera.target_x = -2.5e1\r\n");
    ConfigFile example(path);
    const auto& settings = example.settings();
    require(example.warnings().empty(), "User examples must parse without warnings");
    require(settings.boids.cohesionWeight == 0.11F && settings.boids.minSpeed == 8.0F &&
                settings.boids.maxSpeed == 14.0F, "User's numeric configuration must load");
    require(settings.view.fullscreen && settings.view.agentShape == 0 && !settings.view.grid &&
                !settings.view.bounds && settings.view.panelVisible, "View keys and boolean aliases must load");
    require(!settings.boids.gpu && settings.boids.bounce && settings.boids.cursor &&
                settings.boids.cursorAttract, "Named Boids modes must load");
    require(settings.camera.targetX == -25.0F, "Signed scientific notation must load");
    for (const auto& limits : {
             "scenarios.boids.min_speed = 14\nscenarios.boids.max_speed = 20\n",
             "scenarios.boids.max_speed = 20\nscenarios.boids.min_speed = 14\n",
             "scenarios.boids.max_speed = 0.5\nscenarios.boids.min_speed = 0\n",
             "scenarios.boids.min_speed = 0\nscenarios.boids.max_speed = 0.5\n"}) {
        write(path, limits);
        ConfigFile order(path);
        require(order.warnings().empty(), "Speed limits must validate after all assignments");
    }
}

void invalidValuesKeepPrevious(const std::filesystem::path& directory) {
    const auto path = directory / "invalid.cfg";
    write(path, "scenarios.boids.cohesion_weight = 0.8\n"
                "scenarios.boids.cohesion_weight = nan\n"
                "scenarios.boids.cohesion_weight = 1.3garbage\n"
                "scenarios.boids.count = 8.5\n"
                "scenarios.boids.count = 21474836480\n"
                "scenarios.boids.count = 100001\n"
                "scenarios.boids.alignment_radius = -1\n"
                "scenarios.boids.separation_weight = inf\n"
                "scenarios.boids.max_acceleration = 1e100\n"
                "scenarios.boids.cursor.radius = 21\n"
                "scenarios.boids.cursor.strength = 101\n"
                "scenarios.boids.cursor.plane_height = -40\n"
                "view.fullscreen = yes\n"
                "view.agent_shape = triangle\n"
                "view.color = DIRECTION\n"
                "view.agent_size = 0.01\n"
                "view.camera.yaw = 4\n"
                "view.camera.pitch = 1.57\n"
                "view.camera.distance = 2001\n"
                "view.camera.target_z = -100001\n"
                "scenarios.preview.count = -1\n"
                "scenarios.preview.color = density\n"
                "scenarios.preview.agent_size = 2\n"
                "view.grid =\n"
                "future.unknown = keep\n"
                "broken syntax\n");
    ConfigFile config(path);
    auto expected = Settings{};
    expected.boids.cohesionWeight = 0.8F;
    require(config.settings() == expected, "Invalid values must retain defaults or last valid assignment");
    require(config.warnings().size() == 25, "Every invalid or unknown entry needs a diagnostic");
    require(config.warnings().front().find("Line 2 (scenarios.boids.cohesion_weight)") != std::string::npos,
            "Diagnostics must name the source line and key");
    require(config.lastError().empty(), "Parse diagnostics must not disable saving");
    write(path, "scenarios.boids.min_speed = 20\nscenarios.boids.max_speed = 14\n");
    ConfigFile invalidPair(path);
    require(invalidPair.settings().boids.minSpeed == Settings{}.boids.minSpeed &&
                invalidPair.settings().boids.maxSpeed == Settings{}.boids.maxSpeed &&
                invalidPair.warnings().size() == 1, "Inconsistent speed pair must restore valid defaults");
    write(path, "scenarios.boids.min_speed = 8\nscenarios.boids.max_speed = 14\n"
                "scenarios.boids.min_speed = 20\n");
    ConfigFile invalidDuplicate(path);
    require(invalidDuplicate.settings().boids.minSpeed == 8.0F &&
                invalidDuplicate.settings().boids.maxSpeed == 14.0F,
            "A conflicting duplicate must restore the previous valid speed pair");
}

void completeRoundTripAndComments(const std::filesystem::path& directory) {
    const auto path = directory / "roundtrip.cfg";
    write(path, "# My hand-edited configuration\n"
                "future.plugin = some opaque value ; retain me\n"
                "\n"
                "  view.fullscreen = off # display preference\n"
                "view.fullscreen = off ; duplicate is updated too\n"
                "scenarios.boids.min_speed = 1\n");
    ConfigFile config(path);
    auto& s = config.settings();
    s.scenario = 1;
    s.boids = {99999, false, true, 0.123456789F, 14.5F, 4.9F, 0.5F, 3.1F, 7.0F,
               8.0F, 14.0F, 59.0F, true, true, true, 19.5F, 99.9F, -38.0F};
    s.ants = {19999, 2147483647, true, 19.5F, 9.5F, 1.45F, 11.5F, 7.5F,
              19.5F, 2.9F, 7.9F, false, false, 1.25F, 119.5F, 47};
    s.view = {true, false, false, false, false, 1, 3, 1.25F};
    s.camera = {-1000.0F, 1000.0F, 123.456789F, -2.0F, -0.9F, 1999.5F};
    s.preview = {19999, false, 2, 1.75F};
    require(config.save(), "Complete config must save");
    const auto text = read(path);
    require(text.find("# My hand-edited configuration\n") == 0,
            "Existing full-line comments must survive");
    require(text.find("future.plugin = some opaque value ; retain me\n") != std::string::npos,
            "Unknown settings must survive verbatim");
    require(text.find("app.scenario = ants\n") != std::string::npos &&
                text.find("scenarios.ants.seed = 2147483647\n") != std::string::npos &&
                text.find("scenarios.ants.obstacle_count = 47\n") != std::string::npos &&
                text.find("scenarios.ants.food_spawn_interval = 119.5\n") != std::string::npos,
            "Ant scene selection, seed, obstacle count, and food interval must save exactly");
    require(text.find("  view.fullscreen = on # display preference\n") != std::string::npos &&
                text.find("view.fullscreen = on ; duplicate is updated too\n") != std::string::npos,
            "Known keys must update while retaining comments and indentation");
    ConfigFile reloaded(path);
    require(reloaded.settings() == s, "Every setting must round trip exactly, including float values");
    s.preview.count = 100;
    require(config.save(), "Subsequent changed snapshot must save");
    const auto twice = read(path);
    const std::string key = "scenarios.preview.count = ";
    require(twice.find(key) == twice.rfind(key), "Subsequent saves must not append duplicate default keys");
    require(ConfigFile(path).settings() == s, "Second snapshot must round trip");
    s.view.agentSize = std::numeric_limits<float>::quiet_NaN();
    require(!config.save() && !config.lastError().empty(), "Invalid runtime values must not be saved");
    require(read(path) == twice, "Rejected values must leave existing file intact");
    s.view.agentSize = 1.0F;
    s.view.agentShape = 99;
    require(!config.save() && read(path) == twice, "Invalid runtime enums must leave existing file intact");
}

void particleLifeRoundTripAndValidation(const std::filesystem::path& directory) {
    const auto path = directory / "particle-life.cfg";
    write(path, "# Particle Life rules\n"
                "app.scenario = particle_life\n"
                "scenarios.particle_life.matrix.1.0 = -0.75 # asymmetric rule\n");
    ConfigFile config(path);
    require(config.warnings().empty() && config.settings().scenario == 2,
            "Particle Life must be selectable by its symbolic scenario name");
    auto& life = config.settings().particleLife;
    require(life.gpu, "Particle Life must default to preferring the GPU backend");
    require(!life.threeDimensional && !config.hasValue("scenarios.particle_life.dimensions") &&
                !config.hasValue("scenarios.particle_life.interaction_radius") &&
                config.hasValue("scenarios.particle_life.matrix.1.0"),
            "Old configurations must remain 2D and identify explicitly supplied settings");
    require(life.matrix[8] == -0.75F && life.matrix[1] == 0.0F && !life.customMatrix,
            "Matrix rows must identify receiving species with a fixed stride of eight");
    life.count = 10000;
    life.gpu = false;
    life.threeDimensional = true;
    life.seed = std::numeric_limits<int>::max();
    life.speciesCount = 8;
    life.paused = true;
    life.interactionRadius = 29.25F;
    life.force = 99.5F;
    life.friction = 19.5F;
    life.repulsion = 4.5F;
    life.coreRatio = 0.75F;
    life.maxSpeed = 98.5F;
    life.agentSize = 1.25F;
    life.customMatrix = true;
    for (std::size_t index = 0; index < life.matrix.size(); ++index) {
        life.matrix[index] = static_cast<float>(index) / 31.5F - 1.0F;
    }
    require(config.save(), "Particle Life settings must save");
    const auto saved = read(path);
    require(saved.find("app.scenario = particle_life\n") != std::string::npos &&
                saved.find("scenarios.particle_life.backend = cpu\n") != std::string::npos &&
                saved.find("scenarios.particle_life.dimensions = 3d\n") != std::string::npos &&
                saved.find("scenarios.particle_life.custom_matrix = on\n") != std::string::npos &&
                saved.find("# asymmetric rule\n") != std::string::npos,
            "Saving Particle Life must retain selection, custom rules, and matrix comments");
    ConfigFile reloaded(path);
    require(reloaded.settings() == config.settings() && reloaded.warnings().empty(),
            "Every Particle Life setting and directed matrix entry must round trip exactly");
    require(config.hasValue("scenarios.particle_life.interaction_radius") &&
                reloaded.hasValue("scenarios.particle_life.dimensions"),
            "Saved parameters must be distinguishable from startup defaults");
    life.matrix[63] = std::numeric_limits<float>::infinity();
    require(!config.save() && read(path) == saved,
            "Non-finite runtime matrix values must leave the saved configuration intact");
    life.matrix[63] = 1.01F;
    require(!config.save() && read(path) == saved,
            "Out-of-range runtime matrix values must leave the saved configuration intact");

    const std::string invalidEntries[] = {
        "app.scenario = particle-life",
        "scenarios.particle_life.count = -1",
        "scenarios.particle_life.count = 10001",
        "scenarios.particle_life.count = 5.5",
        "scenarios.particle_life.backend = GPU",
        "scenarios.particle_life.backend = vulkan",
        "scenarios.particle_life.backend = true",
        "scenarios.particle_life.dimensions = 3D",
        "scenarios.particle_life.dimensions = 4d",
        "scenarios.particle_life.dimensions = true",
        "scenarios.particle_life.seed = -1",
        "scenarios.particle_life.seed = 2147483648",
        "scenarios.particle_life.species_count = 0",
        "scenarios.particle_life.species_count = 9",
        "scenarios.particle_life.paused = yes",
        "scenarios.particle_life.interaction_radius = 0.99",
        "scenarios.particle_life.interaction_radius = 30.1",
        "scenarios.particle_life.force = -0.01",
        "scenarios.particle_life.force = 100.1",
        "scenarios.particle_life.friction = -0.01",
        "scenarios.particle_life.friction = 20.1",
        "scenarios.particle_life.repulsion = -0.01",
        "scenarios.particle_life.repulsion = 5.1",
        "scenarios.particle_life.core_ratio = 0.049",
        "scenarios.particle_life.core_ratio = 0.81",
        "scenarios.particle_life.max_speed = 0.99",
        "scenarios.particle_life.max_speed = 100.1",
        "scenarios.particle_life.agent_size = 0.09",
        "scenarios.particle_life.agent_size = 1.51",
        "scenarios.particle_life.custom_matrix = maybe",
        "scenarios.particle_life.matrix.0.1 = -1.01",
        "scenarios.particle_life.matrix.0.1 = 1.01",
        "scenarios.particle_life.matrix.0.1 = nan",
        "scenarios.particle_life.matrix.0.1 = inf",
        "scenarios.particle_life.matrix.0.1 = 0.5garbage",
        "scenarios.particle_life.matrix.8.0 = 0.5",
        "scenarios.particle_life.matrix.0.8 = 0.5",
    };
    std::string input = "app.scenario = particle_life\n"
                        "scenarios.particle_life.matrix.0.1 = 0.375\n";
    for (const auto& invalid : invalidEntries) {
        input += invalid + '\n';
    }
    write(path, input);
    ConfigFile invalid(path);
    auto expected = Settings{};
    expected.scenario = 2;
    expected.particleLife.matrix[1] = 0.375F;
    require(invalid.settings() == expected,
            "Invalid Particle Life values must retain defaults or the previous valid rule");
    require(invalid.warnings().size() == std::size(invalidEntries),
            "Every invalid Particle Life value or matrix coordinate needs a diagnostic");
    write(path, "scenarios.particle_life.backend = cpu\n"
                "scenarios.particle_life.backend = invalid\n");
    ConfigFile invalidBackend(path);
    require(!invalidBackend.settings().particleLife.gpu && invalidBackend.warnings().size() == 1,
            "Invalid backend names must retain the last valid CPU preference");
    write(path, "scenarios.particle_life.dimensions = 3d\n"
                "scenarios.particle_life.dimensions = invalid\n"
                "scenarios.particle_life.interaction_radius = 17.5\n");
    ConfigFile invalidDimensions(path);
    require(invalidDimensions.settings().particleLife.threeDimensional &&
                invalidDimensions.settings().particleLife.interactionRadius == 17.5F &&
                invalidDimensions.warnings().size() == 1,
            "Invalid dimensions must retain 3D and preserve an explicitly saved radius");
    write(path, "scenarios.particle_life.dimensions = 3d\n"
                "scenarios.particle_life.dimensions = 2d\n");
    ConfigFile planar(path);
    require(!planar.settings().particleLife.threeDimensional && planar.warnings().empty(),
            "The saved dimension selection must support returning to 2D");
}

void antSettingsValidation(const std::filesystem::path& directory) {
    const auto path = directory / "ants.cfg";
    write(path, "app.scenario = ants\n"
                "app.scenario = ant\n"
                "scenarios.ants.count = 7000\n"
                "scenarios.ants.count = 20001\n"
                "scenarios.ants.count = -1\n"
                "scenarios.ants.seed = 2147483647\n"
                "scenarios.ants.seed = -1\n"
                "scenarios.ants.seed = 2147483648\n"
                "scenarios.ants.obstacle_count = 32\n"
                "scenarios.ants.obstacle_count = -1\n"
                "scenarios.ants.obstacle_count = 49\n"
                "scenarios.ants.obstacle_count = 2.5\n"
                "scenarios.ants.speed = 0\n"
                "scenarios.ants.sensor_distance = 10.1\n"
                "scenarios.ants.sensor_angle = 1.6\n"
                "scenarios.ants.turn_speed = 13\n"
                "scenarios.ants.wander = -0.1\n"
                "scenarios.ants.deposit_rate = 21\n"
                "scenarios.ants.evaporation = 3.1\n"
                "scenarios.ants.diffusion = 8.1\n"
                "scenarios.ants.show_home_pheromone = yes\n"
                "scenarios.ants.show_food_pheromone = maybe\n"
                "scenarios.ants.paused = maybe\n"
                "scenarios.ants.agent_size = 0.01\n"
                "scenarios.ants.speed = nan\n"
                "scenarios.ants.count = 7.5\n"
                "scenarios.ants.food_spawn_interval = 17.25\n"
                "scenarios.ants.food_spawn_interval = 0\n"
                "scenarios.ants.food_spawn_interval = 120.1\n"
                "scenarios.ants.food_spawn_interval = nan\n"
                "scenarios.ants.food_spawn_interval = inf\n");
    ConfigFile config(path);
    auto expected = Settings{};
    expected.scenario = 1;
    expected.ants.count = 7000;
    expected.ants.seed = 2147483647;
    expected.ants.obstacleCount = 32;
    expected.ants.foodSpawnInterval = 17.25F;
    require(config.settings() == expected,
            "Invalid ant settings must preserve the last valid values and defaults");
    require(config.warnings().size() == 26, "Every invalid ant field needs a diagnostic");
    auto& settings = config.settings();
    settings.ants.seed = 0;
    settings.ants.count = 0;
    settings.ants.obstacleCount = 0;
    require(config.save(), "Zero seed and an empty ant world must be valid");
    ConfigFile reloaded(path);
    require(reloaded.settings() == settings && reloaded.warnings().empty(),
            "Saving must normalize invalid duplicates and preserve scene-specific defaults");
    const auto saved = read(path);
    settings.ants.seed = -1;
    require(!config.save() && read(path) == saved, "Invalid runtime ant seeds must not replace the file");
    settings.ants.seed = 0;
    settings.scenario = 5;
    require(!config.save() && read(path) == saved, "Unknown runtime scenes must not replace the file");
    settings.scenario = 1;
    for (const auto invalid : {-1, 49}) {
        settings.ants.obstacleCount = invalid;
        require(!config.save() && read(path) == saved,
                "Invalid runtime obstacle counts must not replace the file");
    }
    settings.ants.obstacleCount = 0;
    for (const auto invalid : {0.0F, 120.1F, std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()}) {
        settings.ants.foodSpawnInterval = invalid;
        require(!config.save() && read(path) == saved,
                "Invalid runtime food intervals must not replace the file");
    }
    for (const auto interval : {1.0F, 120.0F}) {
        settings.ants.foodSpawnInterval = interval;
        require(config.save(), "Both food interval limits must save");
        ConfigFile boundary(path);
        require(boundary.settings() == settings && boundary.warnings().empty(),
                "Both food interval limits must round trip without warnings");
    }
    for (const auto count : {0, 48}) {
        settings.ants.obstacleCount = count;
        require(config.save(), "Both obstacle count limits must save");
        ConfigFile boundary(path);
        require(boundary.settings() == settings && boundary.warnings().empty(),
                "Both obstacle count limits must round trip without warnings");
    }
}

void ant3DSettingsRoundTripAndValidation(const std::filesystem::path& directory) {
    const auto path = directory / "ants-3d.cfg";
    write(path, "# Existing 2D ant settings\n"
                "app.scenario = ants\n"
                "scenarios.ants.count = 1700\n"
                "scenarios.ants.seed = 42\n"
                "scenarios.ants.paused = on\n");
    ConfigFile config(path);
    require(config.warnings().empty() && config.settings().scenario == 1 &&
                config.settings().ants3d == Settings{}.ants3d &&
                !config.hasValue("scenarios.ants3d.count"),
            "Existing 2D ant configurations must retain their scene and independent 3D defaults");
    const auto originalAnts = config.settings().ants;
    config.settings().scenario = 3;
    config.settings().ants3d = {3000, std::numeric_limits<int>::max(), true, true, false, false};
    config.settings().ants3d.movementSpeed = 8.25F;
    config.settings().ants3d.nestExitCount = 2;
    config.settings().ants3d.nestEntryCount = 3;
    config.settings().ants3d.foodSpawnInterval = 17.5F;
    require(config.save(), "Independent 3D ant settings must save");
    auto saved = read(path);
    require(saved.find("app.scenario = ants3d\n") != std::string::npos &&
                saved.find("scenarios.ants3d.count = 3000\n") != std::string::npos &&
                saved.find("scenarios.ants3d.seed = 2147483647\n") != std::string::npos &&
                saved.find("scenarios.ants3d.movement_speed = 8.25\n") != std::string::npos &&
                saved.find("scenarios.ants3d.nest_exit_count = 2\n") != std::string::npos &&
                saved.find("scenarios.ants3d.nest_entry_count = 3\n") != std::string::npos &&
                saved.find("scenarios.ants3d.food_spawn_interval = 17.5\n") != std::string::npos &&
                saved.find("scenarios.ants3d.show_colliders = on\n") != std::string::npos &&
                saved.find("scenarios.ants3d.show_cave_roofs = off\n") != std::string::npos &&
                saved.find("scenarios.ants3d.show_trails = off\n") != std::string::npos,
            "3D ant scene selection and display toggles must save by name");
    ConfigFile reloaded(path);
    require(reloaded.settings() == config.settings() && reloaded.warnings().empty() &&
                reloaded.settings().ants == originalAnts,
            "Every 3D ant setting must round trip without changing the 2D colony");
    for (const auto count : {0, 3000}) {
        config.settings().ants3d.count = count;
        config.settings().ants3d.seed = 0;
        require(config.save(), "Both 3D ant count limits and a zero seed must save");
        ConfigFile boundary(path);
        require(boundary.settings() == config.settings() && boundary.warnings().empty(),
                "3D ant boundary values must round trip");
    }
    for (const bool maximum : {false, true}) {
        auto& ants = config.settings().ants3d;
        ants.movementSpeed = maximum ? 20.0F : 0.5F;
        ants.nestExitCount = maximum ? 4 : 1;
        ants.nestEntryCount = maximum ? 4 : 1;
        ants.foodSpawnInterval = maximum ? 120.0F : 1.0F;
        require(config.save(), "3D ant movement, portal and food interval limits must save");
        ConfigFile boundary(path);
        require(boundary.settings() == config.settings() && boundary.warnings().empty(),
                "3D ant movement, portal and food interval limits must round trip");
    }
    saved = read(path);
    for (const auto count : {-1, 3001}) {
        config.settings().ants3d.count = count;
        require(!config.save() && read(path) == saved,
                "Invalid runtime 3D ant counts must preserve the saved file");
    }
    config.settings().ants3d.count = 600;
    config.settings().ants3d.seed = -1;
    require(!config.save() && read(path) == saved,
            "Invalid runtime 3D ant seeds must preserve the saved file");
    config.settings().ants3d.seed = 0;
    for (const auto speed : {0.49F, 20.01F, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
        config.settings().ants3d.movementSpeed = speed;
        require(!config.save() && read(path) == saved,
                "Invalid runtime 3D ant speeds must preserve the saved file");
    }
    config.settings().ants3d.movementSpeed = 4.7F;
    for (const auto member : {&emergences::config::Ant3DSettings::nestExitCount,
                              &emergences::config::Ant3DSettings::nestEntryCount}) {
        for (const auto count : {-1, 0, 5}) {
            config.settings().ants3d.*member = count;
            require(!config.save() && read(path) == saved,
                    "Invalid runtime 3D ant portal counts must preserve the saved file");
        }
        config.settings().ants3d.*member = 1;
    }
    for (const auto interval : {0.99F, 120.01F, std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        config.settings().ants3d.foodSpawnInterval = interval;
        require(!config.save() && read(path) == saved,
                "Invalid runtime 3D ant food intervals must preserve the saved file");
    }

    const std::string invalidEntries[] = {
        "scenarios.ants3d.count = -1",
        "scenarios.ants3d.count = 3001",
        "scenarios.ants3d.count = 3.5",
        "scenarios.ants3d.seed = -1",
        "scenarios.ants3d.seed = 2147483648",
        "scenarios.ants3d.movement_speed = 0.49",
        "scenarios.ants3d.movement_speed = 20.01",
        "scenarios.ants3d.movement_speed = nan",
        "scenarios.ants3d.movement_speed = inf",
        "scenarios.ants3d.nest_exit_count = -1",
        "scenarios.ants3d.nest_exit_count = 0",
        "scenarios.ants3d.nest_exit_count = 5",
        "scenarios.ants3d.nest_exit_count = 2.5",
        "scenarios.ants3d.nest_entry_count = -1",
        "scenarios.ants3d.nest_entry_count = 0",
        "scenarios.ants3d.nest_entry_count = 5",
        "scenarios.ants3d.nest_entry_count = 2.5",
        "scenarios.ants3d.food_spawn_interval = 0.99",
        "scenarios.ants3d.food_spawn_interval = 120.01",
        "scenarios.ants3d.food_spawn_interval = nan",
        "scenarios.ants3d.food_spawn_interval = inf",
        "scenarios.ants3d.paused = maybe",
        "scenarios.ants3d.show_colliders = yes",
        "scenarios.ants3d.show_cave_roofs = maybe",
        "scenarios.ants3d.show_trails = enabled",
    };
    std::string input = "app.scenario = ants3d\nscenarios.ants3d.count = 900\n"
                        "scenarios.ants3d.movement_speed = 6.25 # travel speed\n"
                        "scenarios.ants3d.nest_exit_count = 2 # exits\n"
                        "scenarios.ants3d.nest_entry_count = 3 # entries\n"
                        "scenarios.ants3d.food_spawn_interval = 18.5 # food timer\n"
                        "scenarios.ants.count = 1700\n";
    for (const auto& invalid : invalidEntries) input += invalid + '\n';
    write(path, input);
    ConfigFile invalid(path);
    auto expected = Settings{};
    expected.scenario = 3;
    expected.ants.count = 1700;
    expected.ants3d.count = 900;
    expected.ants3d.movementSpeed = 6.25F;
    expected.ants3d.nestExitCount = 2;
    expected.ants3d.nestEntryCount = 3;
    expected.ants3d.foodSpawnInterval = 18.5F;
    require(invalid.settings() == expected && invalid.warnings().size() == std::size(invalidEntries),
            "Invalid 3D ant values must retain valid values without affecting the 2D colony");
    invalid.settings().ants3d.movementSpeed = 7.5F;
    invalid.settings().ants3d.nestExitCount = 4;
    invalid.settings().ants3d.nestEntryCount = 2;
    invalid.settings().ants3d.foodSpawnInterval = 30.0F;
    require(invalid.save(), "Valid 3D ant changes must replace invalid saved values");
    saved = read(path);
    require(saved.find("scenarios.ants3d.movement_speed = 7.5 # travel speed\n") != std::string::npos &&
                saved.find("scenarios.ants3d.nest_exit_count = 4 # exits\n") != std::string::npos &&
                saved.find("scenarios.ants3d.nest_entry_count = 2 # entries\n") != std::string::npos &&
                saved.find("scenarios.ants3d.food_spawn_interval = 30 # food timer\n") != std::string::npos,
            "3D ant movement, portal and food interval saves must preserve inline comments");
    ConfigFile corrected(path);
    require(corrected.settings() == invalid.settings() && corrected.warnings().empty(),
            "Saving 3D ant changes must canonicalize invalid duplicate entries");
}

void ioFailures(const std::filesystem::path& directory) {
    ConfigFile unreadable(directory);
    require(!unreadable.lastError().empty() && !unreadable.save(),
            "Unreadable configuration must diagnose and disable saving");
    require(std::filesystem::is_directory(directory), "Unreadable path must remain intact");
    ConfigFile noParent(directory / "missing-parent" / "config.cfg");
    require(!noParent.save() && !noParent.lastError().empty(),
            "A missing output directory must produce a nonthrowing save error");

    const auto path = directory / "protected.cfg";
    write(path, "# Keep this file\nview.fullscreen = off\n");
    const auto original = read(path);
#ifdef _WIN32
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    require(locked != INVALID_HANDLE_VALUE, "Must lock test fixture");
    ConfigFile cannotRead(path);
    const bool readWasBlocked = !cannotRead.lastError().empty() && !cannotRead.save();
    CloseHandle(locked);
    require(readWasBlocked && read(path) == original, "Read failures must not overwrite the original");
    require(!cannotRead.save(), "A failed initial read must remain protected even after unlock");

    ConfigFile cannotReplace(path);
    cannotReplace.settings().view.fullscreen = true;
    locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    require(locked != INVALID_HANDLE_VALUE, "Must lock replacement test fixture");
    const bool replacementBlocked = !cannotReplace.save() && !cannotReplace.lastError().empty();
    CloseHandle(locked);
    require(replacementBlocked && read(path) == original,
            "Failed atomic replacement must preserve the previous configuration");
    for (const auto& child : std::filesystem::directory_iterator(directory)) {
        require(!child.path().filename().native().starts_with(L"protected.cfg.tmp-"),
                "Failed replacement must clean its temporary file");
    }
    require(cannotReplace.save() && cannotReplace.lastError().empty(),
            "Failed writes must be retryable after the failure is resolved");
    require(ConfigFile(path).settings().view.fullscreen, "Retry must persist the pending settings");
#else
    ConfigFile cannotReplace(path);
    cannotReplace.settings().view.fullscreen = true;
    const auto backup = directory / "protected.backup";
    std::filesystem::rename(path, backup);
    std::filesystem::create_directory(path);
    require(!cannotReplace.save() && !cannotReplace.lastError().empty(),
            "Replacement with a directory must fail safely");
    require(read(backup) == original && std::filesystem::is_directory(path),
            "Failed replacement must preserve original content and destination");
#endif
}

void slimeMoldRoundTripAndValidation(const std::filesystem::path& directory) {
    const auto path = directory / "slime-mold.cfg";
    write(path, "# slime mold settings\napp.scenario = slime_mold\n"
        "scenarios.slime_mold.count = 100000\nscenarios.slime_mold.backend = cpu\n"
        "scenarios.slime_mold.seed = 73\nscenarios.slime_mold.food_count = 12\n"
        "scenarios.slime_mold.sensor_angle = 1.2\nscenarios.slime_mold.exposure = 2.5\n"
        "scenarios.slime_mold.show_agents = on\nscenarios.slime_mold.show_food = off\n");
    ConfigFile config(path);
    require(config.warnings().empty() && config.settings().scenario == 4 &&
        config.settings().slimeMold.count == 100000 && !config.settings().slimeMold.gpu &&
        config.settings().slimeMold.seed == 73 && config.settings().slimeMold.foodCount == 12 &&
        config.settings().slimeMold.sensorAngle == 1.2F && config.settings().slimeMold.exposure == 2.5F &&
        config.settings().slimeMold.showAgents && !config.settings().slimeMold.showFood,
        "Slime mold settings failed to load");
    config.settings().slimeMold.gpu = true;
    config.settings().slimeMold.diffusion = 60;
    config.settings().slimeMold.evaporation = 0;
    config.settings().slimeMold.depositRate = 100;
    config.settings().slimeMold.foodAttraction = 0;
    config.settings().slimeMold.paused = true;
    require(config.save(), "Slime mold settings failed to save");
    ConfigFile reloaded(path);
    require(reloaded.settings() == config.settings() && reloaded.warnings().empty(),
        "Slime mold settings failed to round trip");
    require(read(path).find("# slime mold settings") != std::string::npos, "Slime mold save lost comments");
    for (const std::string invalid : {"count = 100001", "food_count = 13", "backend = invalid",
            "sensor_distance = 0", "diffusion = 61", "evaporation = -1", "exposure = nan",
            "deposit_rate = inf", "seed = -1"}) {
        write(path, "scenarios.slime_mold." + invalid + "\n");
        ConfigFile rejected(path);
        require(rejected.warnings().size() == 1 && rejected.settings() == Settings{},
            "Invalid slime mold setting should retain default");
    }
}

}  // namespace

int main() {
    try {
        const ScratchDirectory scratch;
        missingFileCreatesDefaults(scratch.path);
        examplesAndOrder(scratch.path);
        invalidValuesKeepPrevious(scratch.path);
        completeRoundTripAndComments(scratch.path);
        particleLifeRoundTripAndValidation(scratch.path);
        slimeMoldRoundTripAndValidation(scratch.path);
        antSettingsValidation(scratch.path);
        ant3DSettingsRoundTripAndValidation(scratch.path);
        ioFailures(scratch.path);
        std::cout << "Configuration settings tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Configuration settings test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
