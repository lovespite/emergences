#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

struct ImGuiContext;

namespace emergences::core {

class Context;
class Window;

// The Context and Window must outlive this layer. Use on the window's main
// thread; beginFrame() selects the context for the application's ImGui calls.
class ImGuiLayer final {
public:
    ImGuiLayer(const Context& context, const Window& window);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;
    ImGuiLayer(ImGuiLayer&&) = delete;
    ImGuiLayer& operator=(ImGuiLayer&&) = delete;

    // Call after the device is idle, outside a frame, before recording into a
    // replacement render pass. Reconfiguration keeps the GLFW backend alive.
    void configure(VkRenderPass renderPass, std::uint32_t imageCount,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void shutdownRenderer() noexcept;

    void beginFrame();
    void endFrame();
    // Record inside subpass 0 of the render pass supplied to configure().
    void record(VkCommandBuffer commandBuffer);

    [[nodiscard]] bool wantsMouse() const noexcept;
    [[nodiscard]] float wheelDelta() const noexcept;

private:
    void cleanup() noexcept;

    const Context& context_;
    ImGuiContext* imguiContext_ = nullptr;
    bool rendererInitialized_ = false;
    bool frameOpen_ = false;
    bool frameReady_ = false;
};

} // namespace emergences::core
