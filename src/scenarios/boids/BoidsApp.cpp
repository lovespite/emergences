#include "scenarios/boids/BoidsApp.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

#include "core/Context.h"
#include "core/Window.h"
#include "core/TriangleRenderer.h"

namespace emergences::scenarios::boids {

int BoidsApp::runTriangle(const std::filesystem::path& shaderDirectory, bool smokeTest) {
    // Destruction order: renderer, device/surface, then native window.
    core::Window window{1280, 720, EMERGENCES_WINDOW_TITLE_PREFIX "Vulkan Triangle"};
    core::Context context{window};
    std::cout << "Vulkan initialized on: " << context.deviceName() << '\n';
    std::cout << "Validation layer: " << (context.validationEnabled() ? "enabled" : "disabled") << '\n';
#if !defined(NDEBUG)
    if (smokeTest && !context.validationEnabled()) {
        throw std::runtime_error("Debug smoke test requires VK_LAYER_KHRONOS_validation");
    }
#endif

    std::size_t presentedFrames = 0;
    std::size_t generations = 0;
    {
        core::TriangleRenderer renderer{context, window, shaderDirectory};
        std::cout << "Rendering a triangle. Close the window or press Escape to exit.\n";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.shouldClose()) {
                break;
            }
            if (smokeTest && std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("Triangle smoke test timed out");
            }
            if (!renderer.drawFrame()) {
                window.waitEvents();
                continue;
            }
            ++presentedFrames;
            if (smokeTest) {
                if (presentedFrames == 4) {
                    window.resize(960, 540);
                } else if (presentedFrames == 8) {
                    window.resize(1280, 720);
                } else if (presentedFrames >= 12 && renderer.swapchainGeneration() >= 3) {
                    break;
                }
            }
        }
        context.waitIdle();
        generations = renderer.swapchainGeneration();
    }

    if (context.validationErrorCount() != 0) {
        throw std::runtime_error("Vulkan validation reported " +
                                 std::to_string(context.validationErrorCount()) + " error(s)");
    }
    if (smokeTest) {
        if (presentedFrames < 12 || generations < 3) {
            throw std::runtime_error("Smoke test closed before rendering and resize checks completed");
        }
        std::cout << "Triangle smoke test passed: " << presentedFrames << " presented frames, "
                  << generations << " swapchain generations, 0 validation errors.\n";
    }
    return 0;
}

} // namespace emergences::scenarios::boids
