#include "core/Window.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

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
}

Window::~Window() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
}

void Window::pollEvents() const {
    glfwPollEvents();
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_) == GLFW_TRUE; }

void Window::requestClose() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

void Window::waitEvents() const { glfwWaitEventsTimeout(0.1); }

std::pair<int, int> Window::framebufferSize() const {
    if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE) {
        return {0, 0};
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {width, height};
}

void Window::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Window dimensions must be positive");
    }
    glfwSetWindowSize(window_, width, height);
}

} // namespace emergences::core
