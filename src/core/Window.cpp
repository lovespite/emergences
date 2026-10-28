#include "core/Window.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace emergences::core {

namespace {

std::runtime_error glfwError(const char* operation) {
    const char* description = nullptr;
    glfwGetError(&description);
    return std::runtime_error(std::string(operation) + ": " +
                              (description != nullptr ? description : "unknown GLFW error"));
}

GLFWmonitor* windowMonitor(GLFWwindow* window) {
    int windowX = 0;
    int windowY = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowPos(window, &windowX, &windowY);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    // Choose the monitor containing the largest part of the window. The primary
    // monitor is a fallback for windows moved completely outside the desktop.
    GLFWmonitor* selected = glfwGetPrimaryMonitor();
    std::int64_t largestOverlap = 0;
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    for (int index = 0; index < monitorCount; ++index) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[index]);
        if (mode == nullptr) {
            continue;
        }
        int monitorX = 0;
        int monitorY = 0;
        glfwGetMonitorPos(monitors[index], &monitorX, &monitorY);
        const auto overlapWidth = std::max(0, std::min(windowX + windowWidth, monitorX + mode->width) -
                                                  std::max(windowX, monitorX));
        const auto overlapHeight = std::max(0, std::min(windowY + windowHeight, monitorY + mode->height) -
                                                   std::max(windowY, monitorY));
        const auto overlap = static_cast<std::int64_t>(overlapWidth) * overlapHeight;
        if (overlap > largestOverlap) {
            largestOverlap = overlap;
            selected = monitors[index];
        }
    }
    return selected;
}

} // namespace

namespace detail {

class GlfwRuntime final {
public:
    GlfwRuntime() {
        if (glfwInit() != GLFW_TRUE) {
            throw glfwError("Failed to initialize GLFW");
        }
    }

    ~GlfwRuntime() { glfwTerminate(); }

    GlfwRuntime(const GlfwRuntime&) = delete;
    GlfwRuntime& operator=(const GlfwRuntime&) = delete;
};

} // namespace detail

Window::Window(int width, int height, std::string_view title) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Window dimensions must be positive");
    }

    // GLFW initialization and window operations are main-thread-only, so this
    // weak reference does not need synchronization or own process-exit cleanup.
    static std::weak_ptr<detail::GlfwRuntime> sharedRuntime;
    runtime_ = sharedRuntime.lock();
    if (!runtime_) {
        runtime_ = std::make_shared<detail::GlfwRuntime>();
        sharedRuntime = runtime_;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    const std::string ownedTitle(title);
    window_ = glfwCreateWindow(width, height, ownedTitle.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        throw glfwError("Failed to create a GLFW window");
    }

    glfwSetWindowUserPointer(window_, this);
    // Install before ImGui's backend so it chains this callback. Ignore key
    // repeats and defer mode changes until GLFW has finished dispatching events.
    glfwSetKeyCallback(window_, [](GLFWwindow* nativeWindow, int key, int, int action, int) {
        if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
            auto* window = static_cast<Window*>(glfwGetWindowUserPointer(nativeWindow));
            window->fullscreenTogglePending_ = !window->fullscreenTogglePending_;
        }
    });
}

Window::~Window() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
}

void Window::pollEvents() {
    glfwPollEvents();
    processShortcuts();
}

void Window::processShortcuts() {
    if (fullscreenTogglePending_) {
        fullscreenTogglePending_ = false;
        toggleFullscreen();
    }
    const bool escapeDown = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escapeClosesWindow_ && escapeDown && !escapeDown_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
    escapeDown_ = escapeDown;
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_) == GLFW_TRUE; }

void Window::requestClose() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

void Window::waitEvents() {
    glfwWaitEventsTimeout(0.1);
    processShortcuts();
}

std::pair<int, int> Window::framebufferSize() const {
    if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE) {
        return {0, 0};
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {width, height};
}

void Window::setTitle(std::string_view title) {
    const std::string ownedTitle(title);
    glfwSetWindowTitle(window_, ownedTitle.c_str());
}

void Window::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Window dimensions must be positive");
    }
    glfwSetWindowSize(window_, width, height);
}

bool Window::isFullscreen() const noexcept { return glfwGetWindowMonitor(window_) != nullptr; }

void Window::toggleFullscreen() { setFullscreen(!isFullscreen()); }

void Window::setFullscreen(bool fullscreen) {
    if (fullscreen == isFullscreen()) {
        return;
    }

    if (fullscreen) {
        GLFWmonitor* monitor = windowMonitor(window_);
        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (mode == nullptr) {
            return;
        }

        windowedMaximized_ = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) == GLFW_TRUE;
        if (windowedMaximized_) {
            // Preserve the normal placement as well, so unmaximizing after a
            // fullscreen round trip still restores the original window size.
            glfwRestoreWindow(window_);
        }
        glfwGetWindowPos(window_, &windowedX_, &windowedY_);
        glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        if (!isFullscreen() && windowedMaximized_) {
            glfwMaximizeWindow(window_);
        }
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_,
                             GLFW_DONT_CARE);
        if (!isFullscreen() && windowedMaximized_) {
            glfwMaximizeWindow(window_);
        }
    }
}

} // namespace emergences::core
