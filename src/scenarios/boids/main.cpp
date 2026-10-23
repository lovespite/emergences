#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

#include "scenarios/boids/BoidsApp.h"
#include "config/AppConfig.h"

namespace {

std::filesystem::path argumentPath(int index, const char* argument) {
#if defined(_WIN32)
    // The narrow CRT argv can lose characters in paths outside the system code
    // page. Read the matching native argument when resolving a settings file.
    int argumentCount = 0;
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> arguments{
        CommandLineToArgvW(GetCommandLineW(), &argumentCount), &LocalFree};
    if (arguments && index < argumentCount) return std::filesystem::path{arguments.get()[index]};
#else
    (void)index;
#endif
    return std::filesystem::path{argument};
}

#if defined(_WIN32)
bool hasOutputHandle(HANDLE handle) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
    SetLastError(ERROR_SUCCESS);
    return GetFileType(handle) != FILE_TYPE_UNKNOWN || GetLastError() == ERROR_SUCCESS;
}

void connectConsoleStream(FILE* stream, DWORD standardHandle, HANDLE inheritedHandle) {
    if (hasOutputHandle(inheritedHandle)) {
        // AttachConsole can replace process handles. Keep redirected output (e.g.
        // CTest pipes) and its already initialized C runtime stream intact.
        SetStdHandle(standardHandle, inheritedHandle);
        return;
    }
#if defined(_MSC_VER)
    FILE* reopened = nullptr;
    (void)freopen_s(&reopened, "CONOUT$", "w", stream);
#else
    (void)std::freopen("CONOUT$", "w", stream);
#endif
}

void connectParentConsole() {
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    const auto error = GetStdHandle(STD_ERROR_HANDLE);
    const bool hasOutput = hasOutputHandle(output);
    const bool hasError = hasOutputHandle(error);
    if (hasOutput && hasError) return;

    // The GUI subsystem never creates a console. Reuse a caller's existing
    // terminal when available, while Explorer launches stay window-only.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    connectConsoleStream(stdout, STD_OUTPUT_HANDLE, hasOutput ? output : nullptr);
    connectConsoleStream(stderr, STD_ERROR_HANDLE, hasError ? error : nullptr);
    std::cout.clear();
    std::cerr.clear();
}
#endif

std::filesystem::path shaderDirectory() {
#if defined(_WIN32)
    // Resolve the executable itself: launching via PATH or from another working
    // directory must still find the adjacent compiled shaders. Keep Unicode paths.
    std::vector<wchar_t> path(512);
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            throw std::runtime_error("Cannot determine executable directory");
        }
        if (length < path.size()) {
            return std::filesystem::path{std::wstring_view{path.data(), length}}.parent_path() / "shaders";
        }
        path.resize(path.size() * 2);
    }
#elif defined(__linux__)
    return std::filesystem::canonical("/proc/self/exe").parent_path() / "shaders";
#else
    return EMERGENCES_SHADER_DIRECTORY;
#endif
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    connectParentConsole();
#endif
    bool smokeTest = false;
    bool renderSmokeTest = false;
    bool boidsSmokeTest = false;
    bool gpuSmokeTest = false;
    bool antsSmokeTest = false;
    bool startAnts = false;
    bool renderPreview = false;
    bool noConfig = false;
    std::optional<std::filesystem::path> configPath;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: emergences [mode] [--config PATH | --no-config]\n"
                         "  --smoke-test  Present 12 triangle frames and verify two window resizes.\n"
                         "  --render-smoke-test  Verify particles, UI, buffer growth and window resizes.\n"
                         "  --boids-smoke-test  Verify live CPU Boids, controls, cursor forces and rendering.\n"
                         "  --gpu-smoke-test  Verify GPU Boids rendering and CPU/GPU state transfers.\n"
                         "  --ants-smoke-test  Verify ants, environment, pheromone trails and scene switches.\n"
                         "  --ants        Start the Ant Colony scene (also available in the Scene menu).\n"
                         "  --render-preview  Show the procedural renderer preview.\n"
                         "  --config PATH  Load and remember settings in this file.\n"
                         "  --no-config   Use defaults without reading or writing settings.\n"
                         "  --version     Print the application version.\n"
                         "  --help        Show this help.\n"
                         "No options: restore the selected scene; defaults to GPU Boids.\n"
                         "Settings: emergences.conf beside the executable; changes are saved automatically.\n"
                         "Smoke tests always ignore settings files.\n"
                         "Controls: F11 toggles fullscreen; F1 shows/hides the control panel; Escape closes.\n";
            return 0;
        }
        if (argument == "--version") {
            std::cout << "emergences " << EMERGENCES_VERSION << '\n';
            return 0;
        }
        if (argument == "--smoke-test") {
            smokeTest = true;
        } else if (argument == "--render-smoke-test") {
            renderSmokeTest = true;
        } else if (argument == "--boids-smoke-test") {
            boidsSmokeTest = true;
        } else if (argument == "--gpu-smoke-test") {
            gpuSmokeTest = true;
        } else if (argument == "--ants-smoke-test") {
            antsSmokeTest = true;
        } else if (argument == "--ants") {
            startAnts = true;
        } else if (argument == "--render-preview") {
            renderPreview = true;
        } else if (argument == "--config") {
            if (configPath || index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--") ||
                std::string_view{argv[index + 1]}.empty()) {
                std::cerr << "--config requires one file path and may only be specified once.\n";
                return 2;
            }
            ++index;
            configPath = argumentPath(index, argv[index]);
        } else if (argument == "--no-config") {
            noConfig = true;
        } else {
            std::cerr << "Unknown argument: " << argument << "\nUse --help for usage.\n";
            return 2;
        }
    }

    if (noConfig && configPath) {
        std::cerr << "Choose either --config or --no-config.\n";
        return 2;
    }

    if (static_cast<int>(smokeTest) + static_cast<int>(renderSmokeTest) +
        static_cast<int>(boidsSmokeTest) + static_cast<int>(gpuSmokeTest) + static_cast<int>(renderPreview) +
        static_cast<int>(antsSmokeTest) + static_cast<int>(startAnts) > 1) {
        std::cerr << "Choose only one application mode.\n";
        return 2;
    }

    try {
        const auto shaders = shaderDirectory();
        std::unique_ptr<emergences::config::ConfigFile> config;
        if (!(smokeTest || renderSmokeTest || boidsSmokeTest || gpuSmokeTest || antsSmokeTest || noConfig)) {
            config = std::make_unique<emergences::config::ConfigFile>(
                configPath ? std::filesystem::absolute(*configPath) : shaders.parent_path() / "emergences.conf");
            const auto settingsPath = config->path().u8string();
            std::cout << "Settings: " << reinterpret_cast<const char*>(settingsPath.c_str()) << '\n';
            for (const auto& warning : config->warnings()) std::cerr << "[Config] " << warning << '\n';
            if (!config->lastError().empty()) std::cerr << "[Config] " << config->lastError() << '\n';
        }
        emergences::scenarios::boids::BoidsApp app;
        if (smokeTest) return app.runTriangle(shaders, true);
        if (renderSmokeTest || renderPreview) return app.runPreview(shaders, renderSmokeTest, config.get());
        return app.run(shaders, boidsSmokeTest, gpuSmokeTest, config.get(), startAnts, antsSmokeTest);
    } catch (const std::exception& error) {
        const std::string message = std::string{"Application failed: "} + error.what();
        std::cerr << message << '\n';
#if defined(_WIN32)
        if (!std::cerr && !(smokeTest || renderSmokeTest || boidsSmokeTest || gpuSmokeTest || antsSmokeTest)) {
            MessageBoxA(nullptr, message.c_str(), "emergences", MB_OK | MB_ICONERROR);
        }
#endif
        return 1;
    }
}
