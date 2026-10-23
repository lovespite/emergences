#include "core/Window.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using emergences::core::Window;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Window managers can apply size, position, and maximization asynchronously.
template <typename Predicate>
void awaitWindow(Window& window, Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        window.pollEvents();
        if (predicate()) {
            return;
        }
        glfwWaitEventsTimeout(0.01);
    } while (std::chrono::steady_clock::now() < deadline);
    require(false, message);
}

struct Bounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const Bounds&) const = default;
};

Bounds bounds(const Window& window) {
    Bounds result;
    glfwGetWindowPos(window.nativeHandle(), &result.x, &result.y);
    glfwGetWindowSize(window.nativeHandle(), &result.width, &result.height);
    return result;
}

void awaitBounds(Window& window, const Bounds& expected, const char* message) {
    awaitWindow(window, [&] { return bounds(window) == expected; }, message);
}

void verifyFullscreen(Window& window) {
    require(window.isFullscreen(), "Window must report fullscreen mode");
    GLFWmonitor* monitor = glfwGetWindowMonitor(window.nativeHandle());
    require(monitor != nullptr, "Fullscreen must attach the native window to a monitor");
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    require(mode != nullptr, "Fullscreen monitor must expose a video mode");
    const int width = mode->width;
    const int height = mode->height;
    awaitWindow(window, [&] {
        const Bounds current = bounds(window);
        return current.width == width && current.height == height;
    }, "Fullscreen window must cover the monitor video mode");
}

Bounds placeWindow(Window& window) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    require(monitor != nullptr, "Window tests require a desktop monitor");
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
    const Bounds expected{x + 40, y + 60, std::min(640, width / 2),
                          std::min(360, height / 2)};
    window.resize(expected.width, expected.height);
    glfwSetWindowPos(window.nativeHandle(), expected.x, expected.y);
    awaitBounds(window, expected, "Initial window geometry must be applied");
    return expected;
}

void fullscreenAndRestore(Window& window, const Bounds& normal) {
    require(!window.isFullscreen(), "New windows must begin in windowed mode");
    window.setFullscreen(false);
    awaitBounds(window, normal, "Setting windowed mode twice must preserve geometry");

    window.setFullscreen(true);
    verifyFullscreen(window);
    GLFWmonitor* monitor = glfwGetWindowMonitor(window.nativeHandle());
    window.setFullscreen(true);
    require(glfwGetWindowMonitor(window.nativeHandle()) == monitor,
            "Setting fullscreen twice must retain its monitor");
    window.setFullscreen(false);
    require(!window.isFullscreen() && glfwGetWindowMonitor(window.nativeHandle()) == nullptr,
            "Leaving fullscreen must detach the monitor");
    awaitBounds(window, normal, "Leaving fullscreen must restore window position and size");

    window.toggleFullscreen();
    verifyFullscreen(window);
    window.toggleFullscreen();
    awaitBounds(window, normal, "Repeated fullscreen transitions must retain normal geometry");
}

void maximizedRestore(Window& window, const Bounds& normal) {
    glfwMaximizeWindow(window.nativeHandle());
    awaitWindow(window, [&] {
        return glfwGetWindowAttrib(window.nativeHandle(), GLFW_MAXIMIZED) == GLFW_TRUE;
    }, "Window must maximize before testing fullscreen restoration");

    window.setFullscreen(true);
    verifyFullscreen(window);
    window.setFullscreen(false);
    awaitWindow(window, [&] {
        return !window.isFullscreen() &&
               glfwGetWindowAttrib(window.nativeHandle(), GLFW_MAXIMIZED) == GLFW_TRUE;
    }, "Leaving fullscreen must restore maximized state");
    glfwRestoreWindow(window.nativeHandle());
    awaitBounds(window, normal, "Restoring a maximized window must retain its normal geometry");
}

void sendKey(Window& window, int key, int action) {
    // GLFW has no event injection API. Invoke the callback actually installed on
    // this window, which also exercises Dear ImGui's callback chaining below.
    GLFWkeyfun callback = glfwSetKeyCallback(window.nativeHandle(), nullptr);
    glfwSetKeyCallback(window.nativeHandle(), callback);
    require(callback != nullptr, "The window must install a keyboard callback");
    callback(window.nativeHandle(), key, glfwGetKeyScancode(key), action, 0);
    window.pollEvents();
}

void keyboardToggle(Window& window, const Bounds& normal) {
    sendKey(window, GLFW_KEY_F10, GLFW_PRESS);
    sendKey(window, GLFW_KEY_F10, GLFW_RELEASE);
    require(!window.isFullscreen(), "Unrelated keys must not toggle fullscreen");

    sendKey(window, GLFW_KEY_F11, GLFW_PRESS);
    verifyFullscreen(window);
    sendKey(window, GLFW_KEY_F11, GLFW_REPEAT);
    require(window.isFullscreen(), "Holding F11 must not repeatedly toggle fullscreen");
    sendKey(window, GLFW_KEY_F11, GLFW_RELEASE);
    require(window.isFullscreen(), "Releasing F11 must not toggle fullscreen");

    sendKey(window, GLFW_KEY_F11, GLFW_PRESS);
    require(!window.isFullscreen(), "A second F11 press must return to windowed mode");
    sendKey(window, GLFW_KEY_F11, GLFW_REPEAT);
    sendKey(window, GLFW_KEY_F11, GLFW_RELEASE);
    require(!window.isFullscreen(), "F11 repeat and release must preserve windowed mode");
    awaitBounds(window, normal, "F11 must restore the original window geometry");
}

class ImGuiInput final {
public:
    explicit ImGuiInput(Window& window) {
        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        require(context_ != nullptr, "ImGui context creation must succeed");
        ImGui::GetIO().IniFilename = nullptr;
        if (!ImGui_ImplGlfw_InitForVulkan(window.nativeHandle(), true)) {
            ImGui::DestroyContext(context_);
            throw std::runtime_error("ImGui GLFW initialization must succeed");
        }
    }

    ~ImGuiInput() {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(context_);
    }

    ImGuiInput(const ImGuiInput&) = delete;
    ImGuiInput& operator=(const ImGuiInput&) = delete;

private:
    ImGuiContext* context_ = nullptr;
};

} // namespace

int main() {
    try {
        Window window(640, 360, "Emergences window integration tests");
        const Bounds normal = placeWindow(window);
        fullscreenAndRestore(window, normal);
        maximizedRestore(window, normal);
        keyboardToggle(window, normal);
        {
            ImGuiInput input(window);
            ImGui::GetIO().WantCaptureKeyboard = true;
            keyboardToggle(window, normal);
        }
        // ImGui shutdown must restore the application's original callback.
        keyboardToggle(window, normal);
        std::cout << "Window integration tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Window integration test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
