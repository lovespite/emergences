#pragma once

#include <memory>
#include <string_view>
#include <utility>

struct GLFWwindow;

namespace emergences::core {

namespace detail {
class GlfwRuntime;
}

// Construct, poll, and destroy windows on the application's main thread.
class Window final {
public:
    Window(int width, int height, std::string_view title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    void pollEvents();
    void waitEvents();
    [[nodiscard]] std::pair<int, int> framebufferSize() const;
    void resize(int width, int height);
    void setFullscreen(bool fullscreen);
    void toggleFullscreen();
    [[nodiscard]] bool isFullscreen() const noexcept;
    [[nodiscard]] bool shouldClose() const;
    void requestClose();
    [[nodiscard]] GLFWwindow* nativeHandle() const noexcept { return window_; }

private:
    void processShortcuts();

    // Shared across this wrapper's windows so one window cannot terminate GLFW
    // while another still owns a native window.
    std::shared_ptr<detail::GlfwRuntime> runtime_;
    GLFWwindow* window_ = nullptr;
    int windowedX_ = 0;
    int windowedY_ = 0;
    int windowedWidth_ = 0;
    int windowedHeight_ = 0;
    bool windowedMaximized_ = false;
    bool fullscreenTogglePending_ = false;
};

} // namespace emergences::core
