#include "core/ImGuiLayer.h"

#include "core/Context.h"
#include "core/Window.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <stdexcept>
#include <string>

namespace emergences::core {

namespace {

void checkImGuiVulkanResult(VkResult result) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error("ImGui Vulkan operation failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

} // namespace

ImGuiLayer::ImGuiLayer(const Context& context, const Window& window) : context_(context) {
    IMGUI_CHECKVERSION();
    try {
        imguiContext_ = ImGui::CreateContext();
        if (imguiContext_ == nullptr) {
            throw std::runtime_error("Failed to create the ImGui context");
        }
        ImGui::SetCurrentContext(imguiContext_);
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Keep the initial control panel deterministic without a working-directory file.
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplGlfw_InitForVulkan(window.nativeHandle(), true)) {
            throw std::runtime_error("Failed to initialize the ImGui GLFW backend");
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

ImGuiLayer::~ImGuiLayer() { cleanup(); }

void ImGuiLayer::configure(VkRenderPass renderPass, std::uint32_t imageCount,
                           VkSampleCountFlagBits samples) {
    if (renderPass == VK_NULL_HANDLE || imageCount < 2) {
        throw std::invalid_argument("ImGui requires a render pass and at least two swapchain images");
    }
    if (frameOpen_) {
        throw std::logic_error("Cannot reconfigure ImGui during an active frame");
    }

    ImGui::SetCurrentContext(imguiContext_);
    shutdownRenderer();
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = context_.instance();
    info.PhysicalDevice = context_.physicalDevice();
    info.Device = context_.device();
    info.QueueFamily = context_.graphicsQueueFamily();
    info.Queue = context_.graphicsQueue();
    info.DescriptorPoolSize = 32;
    info.RenderPass = renderPass;
    info.MinImageCount = 2;
    info.ImageCount = imageCount;
    info.MSAASamples = samples;
    info.CheckVkResultFn = checkImGuiVulkanResult;

    try {
        if (!ImGui_ImplVulkan_Init(&info)) {
            throw std::runtime_error("Failed to initialize the ImGui Vulkan backend");
        }
        // v1.91.9b owns the upload commands and waits for completion here. A
        // fresh backend needs a fresh font texture even though the UI context
        // and its CPU font atlas are preserved across swapchain recreation.
        if (!ImGui_ImplVulkan_CreateFontsTexture()) {
            throw std::runtime_error("Failed to upload the ImGui font atlas");
        }
        rendererInitialized_ = true;
    } catch (...) {
        // Init can publish BackendRendererUserData before a Vulkan allocation
        // fails, so shutdownRenderer() also handles partially initialized state.
        static_cast<void>(vkDeviceWaitIdle(context_.device()));
        shutdownRenderer();
        throw;
    }
}

void ImGuiLayer::shutdownRenderer() noexcept {
    if (imguiContext_ == nullptr) {
        return;
    }
    ImGui::SetCurrentContext(imguiContext_);
    if (frameOpen_) {
        ImGui::EndFrame();
        frameOpen_ = false;
    }
    frameReady_ = false;
    if (ImGui::GetIO().BackendRendererUserData != nullptr) {
        ImGui_ImplVulkan_Shutdown();
    }
    rendererInitialized_ = false;
}

void ImGuiLayer::beginFrame() {
    if (!rendererInitialized_) {
        throw std::logic_error("Configure the ImGui renderer before beginning a frame");
    }
    if (frameOpen_) {
        throw std::logic_error("The previous ImGui frame is still active");
    }
    ImGui::SetCurrentContext(imguiContext_);
    frameReady_ = false;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    frameOpen_ = true;
}

void ImGuiLayer::endFrame() {
    if (!frameOpen_) {
        throw std::logic_error("No ImGui frame is active");
    }
    ImGui::SetCurrentContext(imguiContext_);
    ImGui::Render();
    frameOpen_ = false;
    frameReady_ = true;
}

void ImGuiLayer::record(VkCommandBuffer commandBuffer) {
    if (!rendererInitialized_ || !frameReady_ || frameOpen_) {
        throw std::logic_error("Finish the ImGui frame before recording its draw commands");
    }
    if (commandBuffer == VK_NULL_HANDLE) {
        throw std::invalid_argument("ImGui requires a valid command buffer");
    }
    ImGui::SetCurrentContext(imguiContext_);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

bool ImGuiLayer::wantsMouse() const noexcept {
    ImGui::SetCurrentContext(imguiContext_);
    return ImGui::GetIO().WantCaptureMouse;
}

float ImGuiLayer::wheelDelta() const noexcept {
    ImGui::SetCurrentContext(imguiContext_);
    return ImGui::GetIO().MouseWheel;
}

void ImGuiLayer::cleanup() noexcept {
    if (imguiContext_ == nullptr) {
        return;
    }
    // The device can be lost during error unwinding; still release all owners.
    static_cast<void>(vkDeviceWaitIdle(context_.device()));
    shutdownRenderer();
    if (ImGui::GetIO().BackendPlatformUserData != nullptr) {
        ImGui_ImplGlfw_Shutdown();
    }
    ImGui::DestroyContext(imguiContext_);
    imguiContext_ = nullptr;
}

} // namespace emergences::core
