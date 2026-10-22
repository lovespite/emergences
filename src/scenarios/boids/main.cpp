#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "scenarios/boids/BoidsApp.h"

namespace {

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
    bool smokeTest = false;
    bool renderSmokeTest = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: emergences [--smoke-test | --render-smoke-test | --version | --help]\n"
                         "  --smoke-test  Present 12 triangle frames and verify two window resizes.\n"
                         "  --render-smoke-test  Verify particles, UI, buffer growth and window resizes.\n"
                         "  --version     Print the application version.\n"
                         "No options: interactive particle rendering preview (Phase 1).\n";
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
        } else {
            std::cerr << "Unknown argument: " << argument << "\nUse --help for usage.\n";
            return 2;
        }
    }

    if (smokeTest && renderSmokeTest) {
        std::cerr << "Choose only one smoke test mode.\n";
        return 2;
    }

    try {
        emergences::scenarios::boids::BoidsApp app;
        return smokeTest ? app.run(shaderDirectory(), true)
                         : app.runPreview(shaderDirectory(), renderSmokeTest);
    } catch (const std::exception& error) {
        std::cerr << "Application failed: " << error.what() << '\n';
        return 1;
    }
}
