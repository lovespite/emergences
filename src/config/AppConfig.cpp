#include "config/AppConfig.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace emergences::config {
namespace {

using Value = std::variant<bool*, int*, float*>;

struct Field {
    std::string key;
    Value value;
    float minimum = 0.0F;
    float maximum = 0.0F;
    std::string_view choices{};
};

std::vector<Field> fields(Settings& settings) {
    auto& b = settings.boids;
    auto& a = settings.ants;
    auto& life = settings.particleLife;
    auto& v = settings.view;
    auto& c = settings.camera;
    auto& p = settings.preview;
    constexpr float pi = 3.14159265358979323846F;
    std::vector<Field> result{
        {"app.scenario", &settings.scenario, 0.0F, 0.0F, "boids|ants|particle_life"},
        {"scenarios.boids.count", &b.count, 0.0F, 100000.0F},
        {"scenarios.boids.backend", &b.gpu, 0.0F, 0.0F, "cpu|gpu"},
        {"scenarios.boids.paused", &b.paused},
        {"scenarios.boids.alignment_weight", &b.alignmentWeight, 0.0F, 5.0F},
        {"scenarios.boids.alignment_radius", &b.alignmentRadius, 0.0F, 15.0F},
        {"scenarios.boids.cohesion_weight", &b.cohesionWeight, 0.0F, 5.0F},
        {"scenarios.boids.cohesion_radius", &b.cohesionRadius, 0.0F, 15.0F},
        {"scenarios.boids.separation_weight", &b.separationWeight, 0.0F, 5.0F},
        {"scenarios.boids.separation_radius", &b.separationRadius, 0.0F, 15.0F},
        {"scenarios.boids.min_speed", &b.minSpeed, 0.0F, 30.0F},
        {"scenarios.boids.max_speed", &b.maxSpeed, 0.1F, 30.0F},
        {"scenarios.boids.max_acceleration", &b.maxAcceleration, 0.0F, 60.0F},
        {"scenarios.boids.boundary", &b.bounce, 0.0F, 0.0F, "wrap|bounce"},
        {"scenarios.boids.cursor.enabled", &b.cursor},
        {"scenarios.boids.cursor.mode", &b.cursorAttract, 0.0F, 0.0F, "repel|attract"},
        {"scenarios.boids.cursor.radius", &b.cursorRadius, 0.0F, 20.0F},
        {"scenarios.boids.cursor.strength", &b.cursorStrength, 0.0F, 100.0F},
        {"scenarios.boids.cursor.plane_height", &b.cursorPlaneHeight, -39.0F, 39.0F},
        {"scenarios.ants.count", &a.count, 0.0F, 20000.0F},
        {"scenarios.ants.seed", &a.seed, 0.0F, static_cast<float>(std::numeric_limits<int>::max())},
        {"scenarios.ants.obstacle_count", &a.obstacleCount, 0.0F, 48.0F},
        {"scenarios.ants.paused", &a.paused},
        {"scenarios.ants.speed", &a.speed, 0.1F, 20.0F},
        {"scenarios.ants.sensor_distance", &a.sensorDistance, 0.1F, 10.0F},
        {"scenarios.ants.sensor_angle", &a.sensorAngle, 0.05F, 1.5F},
        {"scenarios.ants.turn_speed", &a.turnSpeed, 0.1F, 12.0F},
        {"scenarios.ants.wander", &a.wander, 0.0F, 8.0F},
        {"scenarios.ants.deposit_rate", &a.depositRate, 0.0F, 20.0F},
        {"scenarios.ants.evaporation", &a.evaporation, 0.0F, 3.0F},
        {"scenarios.ants.diffusion", &a.diffusion, 0.0F, 8.0F},
        {"scenarios.ants.show_home_pheromone", &a.showHomePheromone},
        {"scenarios.ants.show_food_pheromone", &a.showFoodPheromone},
        {"scenarios.ants.agent_size", &a.agentSize, 0.1F, 1.5F},
        {"scenarios.ants.food_spawn_interval", &a.foodSpawnInterval, 1.0F, 120.0F},
        {"scenarios.particle_life.count", &life.count, 0.0F, 10000.0F},
        {"scenarios.particle_life.backend", &life.gpu, 0.0F, 0.0F, "cpu|gpu"},
        {"scenarios.particle_life.dimensions", &life.threeDimensional, 0.0F, 0.0F, "2d|3d"},
        {"scenarios.particle_life.seed", &life.seed, 0.0F, static_cast<float>(std::numeric_limits<int>::max())},
        {"scenarios.particle_life.species_count", &life.speciesCount, 1.0F, 8.0F},
        {"scenarios.particle_life.paused", &life.paused},
        {"scenarios.particle_life.interaction_radius", &life.interactionRadius, 1.0F, 30.0F},
        {"scenarios.particle_life.force", &life.force, 0.0F, 100.0F},
        {"scenarios.particle_life.friction", &life.friction, 0.0F, 20.0F},
        {"scenarios.particle_life.repulsion", &life.repulsion, 0.0F, 5.0F},
        {"scenarios.particle_life.core_ratio", &life.coreRatio, 0.05F, 0.8F},
        {"scenarios.particle_life.max_speed", &life.maxSpeed, 1.0F, 100.0F},
        {"scenarios.particle_life.agent_size", &life.agentSize, 0.1F, 1.5F},
        {"scenarios.particle_life.custom_matrix", &life.customMatrix},
        {"view.fullscreen", &v.fullscreen},
        {"view.panel_visible", &v.panelVisible},
        {"view.antialiasing", &v.antialiasing},
        {"view.grid", &v.grid},
        {"view.bounds", &v.bounds},
        {"view.agent_shape", &v.agentShape, 0.0F, 0.0F, "arrow|dot"},
        {"view.color", &v.colorMode, 0.0F, 0.0F, "direction|speed|density|solid"},
        {"view.agent_size", &v.agentSize, 0.1F, 1.5F},
        {"view.camera.target_x", &c.targetX, -100000.0F, 100000.0F},
        {"view.camera.target_y", &c.targetY, -100000.0F, 100000.0F},
        {"view.camera.target_z", &c.targetZ, -100000.0F, 100000.0F},
        {"view.camera.yaw", &c.yaw, -pi, pi},
        {"view.camera.pitch", &c.pitch, -(pi / 2.0F - 0.01F), pi / 2.0F - 0.01F},
        {"view.camera.distance", &c.distance, 3.0F, 2000.0F},
        {"scenarios.preview.count", &p.count, 0.0F, 20000.0F},
        {"scenarios.preview.animate", &p.animate},
        {"scenarios.preview.color", &p.colorMode, 0.0F, 0.0F, "direction|speed|solid"},
        {"scenarios.preview.agent_size", &p.agentSize, 0.15F, 1.8F},
    };
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            result.push_back({"scenarios.particle_life.matrix." + std::to_string(row) + "." +
                                  std::to_string(column),
                              &life.matrix[row * 8 + column], -1.0F, 1.0F});
        }
    }
    return result;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string_view choiceAt(std::string_view choices, int index) {
    for (int current = 0; !choices.empty(); ++current) {
        const auto separator = choices.find('|');
        if (current == index) {
            return choices.substr(0, separator);
        }
        if (separator == std::string_view::npos) {
            break;
        }
        choices.remove_prefix(separator + 1);
    }
    return {};
}

bool parse(Field& field, std::string_view text) {
    if (!field.choices.empty()) {
        for (int index = 0;; ++index) {
            const auto choice = choiceAt(field.choices, index);
            if (choice.empty()) {
                return false;
            }
            if (text == choice) {
                if (auto boolean = std::get_if<bool*>(&field.value)) {
                    **boolean = index != 0;
                } else {
                    *std::get<int*>(field.value) = index;
                }
                return true;
            }
        }
    }
    if (auto boolean = std::get_if<bool*>(&field.value)) {
        if (text == "on" || text == "true" || text == "1") {
            **boolean = true;
            return true;
        }
        if (text == "off" || text == "false" || text == "0") {
            **boolean = false;
            return true;
        }
        return false;
    }
    if (text.empty()) {
        return false;
    }
    // from_chars deliberately avoids dependence on the application's locale.
    if (auto integer = std::get_if<int*>(&field.value)) {
        int number = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
            static_cast<float>(number) < field.minimum ||
            static_cast<float>(number) > field.maximum) {
            return false;
        }
        **integer = number;
        return true;
    }
    float number = 0.0F;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(number) || number < field.minimum || number > field.maximum) {
        return false;
    }
    *std::get<float*>(field.value) = number;
    return true;
}

std::string format(const Field& field) {
    if (!field.choices.empty()) {
        const int index = std::holds_alternative<bool*>(field.value)
                              ? (*std::get<bool*>(field.value) ? 1 : 0)
                              : *std::get<int*>(field.value);
        return std::string(choiceAt(field.choices, index));
    }
    if (const auto boolean = std::get_if<bool*>(&field.value)) {
        return **boolean ? "on" : "off";
    }
    std::array<char, 64> buffer{};
    const auto result = std::holds_alternative<int*>(field.value)
                            ? std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                            *std::get<int*>(field.value))
                            : std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                            *std::get<float*>(field.value));
    return std::string(buffer.data(), result.ptr);
}

struct Entry {
    std::string_view key;
    std::string_view value;
    std::size_t equals = std::string_view::npos;
    std::size_t comment = std::string_view::npos;
};

Entry entry(std::string_view line) {
    Entry result;
    result.comment = line.find_first_of("#;");
    const auto content = line.substr(0, result.comment);
    result.equals = content.find('=');
    if (result.equals != std::string_view::npos) {
        result.key = trim(content.substr(0, result.equals));
        result.value = trim(content.substr(result.equals + 1));
    }
    return result;
}

Field* findField(std::vector<Field>& schema, std::string_view key) {
    for (auto& field : schema) {
        if (field.key == key) {
            return &field;
        }
    }
    return nullptr;
}

std::string lineWarning(std::size_t line, std::string_view key, std::string_view reason) {
    return "Line " + std::to_string(line) + " (" + std::string(key) + "): " + std::string(reason);
}

bool atomicWrite(const std::filesystem::path& path, const std::string& content,
                 std::string& error) {
    static std::atomic<unsigned long long> sequence{0};
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE output = INVALID_HANDLE_VALUE;
#else
    int output = -1;
#endif
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = path;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(sequence.fetch_add(1));
#ifdef _WIN32
        output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output != INVALID_HANDLE_VALUE) {
            break;
        }
        const auto code = GetLastError();
        if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) {
            error = "Cannot create temporary configuration: " +
                    std::error_code(static_cast<int>(code), std::system_category()).message();
            return false;
        }
#else
        output = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (output >= 0) {
            break;
        }
        if (errno != EEXIST) {
            error = "Cannot create temporary configuration: " +
                    std::error_code(errno, std::generic_category()).message();
            return false;
        }
#endif
    }
#ifdef _WIN32
    if (output == INVALID_HANDLE_VALUE) {
#else
    if (output < 0) {
#endif
        error = "Cannot create a unique temporary configuration file";
        return false;
    }
    bool success = true;
    std::size_t offset = 0;
    while (offset < content.size()) {
#ifdef _WIN32
        DWORD written = 0;
        const auto remaining = content.size() - offset;
        const DWORD length = static_cast<DWORD>(remaining > MAXDWORD ? MAXDWORD : remaining);
        if (!WriteFile(output, content.data() + offset, length, &written, nullptr) || written == 0) {
            success = false;
            break;
        }
#else
        const auto written = ::write(output, content.data() + offset, content.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            success = false;
            break;
        }
#endif
        offset += static_cast<std::size_t>(written);
    }
#ifdef _WIN32
    if (success && !FlushFileBuffers(output)) {
        success = false;
    }
    if (!CloseHandle(output)) {
        success = false;
    }
    if (success && !MoveFileExW(temporary.c_str(), path.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace configuration: " +
                std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        success = false;
    }
#else
    if (success && ::fsync(output) != 0) {
        success = false;
    }
    if (::close(output) != 0) {
        success = false;
    }
    if (success) {
        std::error_code renameError;
        std::filesystem::rename(temporary, path, renameError);
        if (renameError) {
            error = "Cannot replace configuration: " + renameError.message();
            success = false;
        }
    }
#endif
    if (!success) {
        if (error.empty()) {
            error = "Cannot write or flush configuration";
        }
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
    }
    return success;
}

}  // namespace

ConfigFile::ConfigFile(std::filesystem::path path) : path_(std::move(path)) {
    load();
}

bool ConfigFile::hasValue(std::string_view key) const {
    return std::any_of(lines_.begin(), lines_.end(), [key](const auto& line) {
        const auto parsed = entry(line);
        return parsed.equals != std::string_view::npos && parsed.key == key;
    });
}

void ConfigFile::load() {
    try {
        std::error_code statusError;
        const auto status = std::filesystem::status(path_, statusError);
        if (status.type() == std::filesystem::file_type::not_found &&
            (!statusError || statusError == std::errc::no_such_file_or_directory)) {
            return;
        }
        if (statusError || !std::filesystem::is_regular_file(status)) {
            readable_ = false;
            lastError_ = statusError ? "Cannot read configuration: " + statusError.message()
                                    : "Configuration path is not a regular file";
            return;
        }
        std::ifstream input(path_, std::ios::binary);
        if (!input) {
            readable_ = false;
            lastError_ = "Cannot open configuration for reading";
            return;
        }
        auto schema = fields(settings_);
        std::string line;
        std::size_t lineNumber = 0;
        std::size_t speedLine = 0;
        std::vector<std::pair<float, float>> speedCandidates{
            {settings_.boids.minSpeed, settings_.boids.maxSpeed}};
        while (std::getline(input, line)) {
            ++lineNumber;
            if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF")) {
                line.erase(0, 3);
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines_.push_back(line);
            const auto parsed = entry(line);
            if (trim(std::string_view(line).substr(0, parsed.comment)).empty()) {
                continue;
            }
            if (parsed.equals == std::string_view::npos || parsed.key.empty()) {
                warnings_.push_back(lineWarning(lineNumber, "", "expected key = value"));
                continue;
            }
            auto* field = findField(schema, parsed.key);
            if (!field) {
                warnings_.push_back(lineWarning(lineNumber, parsed.key, "unknown key; preserved"));
                continue;
            }
            if (!parse(*field, parsed.value)) {
                warnings_.push_back(lineWarning(lineNumber, parsed.key, "invalid value; keeping previous value"));
            } else if (parsed.key == "scenarios.boids.min_speed" ||
                       parsed.key == "scenarios.boids.max_speed") {
                speedLine = lineNumber;
                speedCandidates.emplace_back(settings_.boids.minSpeed, settings_.boids.maxSpeed);
            }
        }
        if (input.bad()) {
            readable_ = false;
            lastError_ = "Cannot finish reading configuration; saving disabled";
            settings_ = Settings{};
            return;
        }
        if (settings_.boids.minSpeed > settings_.boids.maxSpeed) {
            warnings_.push_back(lineWarning(speedLine, "scenarios.boids.min_speed / scenarios.boids.max_speed",
                                           "minimum exceeds maximum; restoring previous valid speed limits"));
            // Validate the completed pair first, so valid values never depend on file order.
            // A conflicting later duplicate falls back to the last valid pair, or defaults.
            for (auto candidate = speedCandidates.rbegin(); candidate != speedCandidates.rend(); ++candidate) {
                if (candidate->first <= candidate->second) {
                    settings_.boids.minSpeed = candidate->first;
                    settings_.boids.maxSpeed = candidate->second;
                    break;
                }
            }
        }
        savedSettings_ = settings_;
        missing_ = false;
    } catch (const std::exception& exception) {
        readable_ = false;
        settings_ = Settings{};
        lastError_ = "Cannot read configuration: " + std::string(exception.what());
    }
}

bool ConfigFile::save() {
    if (!readable_) {
        return false;
    }
    if (!missing_ && settings_ == savedSettings_) {
        lastError_.clear();
        return true;
    }
    lastError_.clear();
    try {
        auto schema = fields(settings_);
        Settings validated;
        auto validationSchema = fields(validated);
        for (std::size_t index = 0; index < schema.size(); ++index) {
            if (!parse(validationSchema[index], format(schema[index]))) {
                lastError_ = "Cannot save invalid value for " + std::string(schema[index].key);
                return false;
            }
        }
        if (settings_.boids.minSpeed > settings_.boids.maxSpeed) {
            lastError_ = "Cannot save speed limits: minimum exceeds maximum";
            return false;
        }
        std::vector<bool> emitted(schema.size(), false);
        std::string content;
        for (const auto& line : lines_) {
            const auto parsed = entry(line);
            auto* field = findField(schema, parsed.key);
            if (field && parsed.equals != std::string_view::npos) {
                const auto index = static_cast<std::size_t>(field - schema.data());
                emitted[index] = true;
                content += line.substr(0, parsed.equals + 1) + " " + format(*field);
                if (parsed.comment != std::string_view::npos) {
                    content += " " + line.substr(parsed.comment);
                }
            } else {
                content += line;
            }
            content += '\n';
        }
        bool headingWritten = false;
        for (std::size_t index = 0; index < schema.size(); ++index) {
            if (!emitted[index]) {
                if (!headingWritten) {
                    if (!content.empty() && !content.ends_with("\n\n")) {
                        content += '\n';
                    }
                    content += "# Emergence settings (saved automatically)\n";
                    headingWritten = true;
                }
                content += std::string(schema[index].key) + " = " + format(schema[index]) + '\n';
            }
        }
        if (!atomicWrite(path_, content, lastError_)) {
            return false;
        }
        // Keep the in-memory template in sync so later saves never duplicate added keys.
        lines_.clear();
        std::size_t start = 0;
        while (start < content.size()) {
            const auto end = content.find('\n', start);
            lines_.push_back(content.substr(start, end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        savedSettings_ = settings_;
        missing_ = false;
        return true;
    } catch (const std::exception& exception) {
        lastError_ = "Cannot save configuration: " + std::string(exception.what());
        return false;
    }
}

}  // namespace emergences::config
