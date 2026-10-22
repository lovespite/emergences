#pragma once

#include "core/FrameRenderer.h"
#include <filesystem>
#include <memory>

namespace emergences::core {
class Context;
class Window;
class Pipeline;

// Original smoke milestone, sharing submission with the particle scene.
class TriangleRenderer final {
public:
    TriangleRenderer(const Context&, const Window&, std::filesystem::path shaderDirectory);
    ~TriangleRenderer();
    [[nodiscard]] bool drawFrame();
    [[nodiscard]] std::size_t swapchainGeneration() const noexcept;
private:
    const Context& context_;
    std::filesystem::path shaderDirectory_;
    std::unique_ptr<Pipeline> pipeline_;
    FrameRenderer renderer_;
};
} // namespace emergences::core
