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

    void pollEvents() const;
    void waitEvents() const;
    [[nodiscard]] std::pair<int, int> framebufferSize() const;
    void resize(int width, int height);
    [[nodiscard]] bool shouldClose() const;
    void requestClose();
    [[nodiscard]] GLFWwindow* nativeHandle() const noexcept { return window_; }

private:
    // Shared across this wrapper's windows so one window cannot terminate GLFW
    // while another still owns a native window.
    std::shared_ptr<detail::GlfwRuntime> runtime_;
    GLFWwindow* window_ = nullptr;
};

} // namespace emergences::core
