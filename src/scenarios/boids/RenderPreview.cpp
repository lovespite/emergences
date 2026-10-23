#include "scenarios/boids/BoidsApp.h"
#include "core/Camera.h"
#include "core/Context.h"
#include "core/FrameRenderer.h"
#include "core/ImGuiLayer.h"
#include "core/LineRenderer.h"
#include "core/ParticleRenderer.h"
#include "core/Window.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace emergences::scenarios::boids {
namespace {
struct Preview {
    bool panelVisible = true;
    int count = 2048;
    int mode = 0;
    int colorMode = 0;
    float scale = 0.65F;
    float time = 0.0F;
    bool animate = true;
    bool grid = true;
    bool bounds = true;
    bool antialiasing = true;
    std::vector<core::Particle> particles;

    void generate() {
        particles.resize(static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < particles.size(); ++i) {
            const float index = static_cast<float>(i);
            const float phase = index * 2.39996323F;
            const float radius = 5.0F + 12.0F * std::sqrt(std::fmod(index * 0.61803399F, 1.0F));
            const float speed = 0.25F + 0.3F * std::fmod(index * 0.75487766F, 1.0F);
            const float angle = phase + time * speed;
            auto& particle = particles[i];
            particle.position = {radius * std::cos(angle),
                12.0F * std::sin(phase * 0.37F) + 2.0F * std::sin(angle), radius * std::sin(angle)};
            particle.velocity = {-radius * speed * std::sin(angle),
                2.0F * speed * std::cos(angle), radius * speed * std::cos(angle)};
            particle.size = 1.0F;
            particle.id = static_cast<std::uint32_t>(i);
            if (colorMode == 2) {
                particle.color = {0.18F, 0.77F, 0.95F};
            } else {
                const float hue = colorMode == 0 ? std::fmod(angle / 6.2831853F + 1.0F, 1.0F)
                                                : 0.65F - 0.6F * (speed - 0.25F) / 0.3F;
                ImGui::ColorConvertHSVtoRGB(hue, 0.65F, 0.95F,
                    particle.color.r, particle.color.g, particle.color.b);
            }
        }
    }
};

void drawPanel(Preview& preview, core::Camera& camera, const core::Context& context,
               const core::FrameRenderer& renderer, bool& fullscreen) {
    ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 0), ImGuiCond_Always);
    ImGui::Begin("Emergences", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::TextColored(ImVec4(0.3F, 0.8F, 0.95F, 1.0F), "PHASE 1 / RENDERING PREVIEW");
    ImGui::Checkbox("Fullscreen (F11)", &fullscreen);
    ImGui::SameLine();
    ImGui::TextDisabled("F1: hide panel");
    ImGui::TextWrapped("Procedural particles for renderer verification. Boids simulation is the next stage.");
    ImGui::Separator();
    ImGui::SliderInt("Particles", &preview.count, 0, 20000);
    preview.count = std::clamp(preview.count, 0, 20000);
    ImGui::Combo("Shape", &preview.mode, "Arrows\0Dots\0");
    const auto samples = renderer.supportedAntialiasingSampleCount();
    ImGui::BeginDisabled(samples == VK_SAMPLE_COUNT_1_BIT);
    ImGui::Checkbox("Anti-aliasing (MSAA)", &preview.antialiasing);
    ImGui::EndDisabled();
    if (samples == VK_SAMPLE_COUNT_1_BIT) ImGui::TextDisabled("MSAA unavailable on this device.");
    else ImGui::TextDisabled("MSAA: %ux when enabled", static_cast<unsigned>(samples));
    ImGui::SliderFloat("Size", &preview.scale, 0.15F, 1.8F, "%.2f");
    preview.scale = std::clamp(preview.scale, 0.15F, 1.8F);
    ImGui::Combo("Color", &preview.colorMode, "Direction\0Speed\0Solid\0");
    ImGui::Checkbox("Animate preview", &preview.animate);
    if (ImGui::Button("Reset preview")) preview.time = 0.0F;
    ImGui::SameLine();
    if (ImGui::Button("Reset camera")) camera.reset();
    ImGui::Separator();
    ImGui::Checkbox("Floor grid", &preview.grid);
    ImGui::SameLine();
    ImGui::Checkbox("World bounds", &preview.bounds);
    ImGui::TextDisabled("Left drag: orbit | Middle drag: pan");
    ImGui::TextDisabled("Wheel: zoom | Escape: close");
    ImGui::Separator();
    ImGui::Text("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));
    ImGui::TextWrapped("%s", context.deviceName().c_str());
    ImGui::TextDisabled("Validation: %s", context.validationEnabled() ? "on" : "off");
    ImGui::End();
}

// Exercise both frame slots with empty/single/large/shrunk arrays, modes, camera,
// and UI buffers. These are deterministic render fixtures, not Boids dynamics.
void smokeSettings(std::size_t frame, Preview& preview, core::Camera& camera) {
    if (frame < 2) preview.count = 0;
    else if (frame < 4) preview.count = 1;
    else if (frame < 6) preview.count = 257;
    // Grow both slots on ordinary frames too, while the other slot may be live.
    else if (frame < 10) preview.count = 8192;
    else if (frame < 14) preview.count = 96;
    else preview.count = 2048;
    preview.mode = frame >= 8 && frame < 18 ? 1 : 0;
    preview.grid = frame < 14 || frame >= 18;
    preview.bounds = frame < 16 || frame >= 18;
    preview.animate = frame < 12 || frame >= 18;
    preview.colorMode = frame >= 18 ? 0 : static_cast<int>((frame / 8) % 3);
    if (frame == 6) { camera.orbit(50.0F, -15.0F); camera.zoom(1.0F); }
    if (frame == 12) camera.pan(20.0F, 10.0F, 540.0F);
    if (frame == 18) camera.reset();
}
} // namespace

int BoidsApp::runPreview(const std::filesystem::path& shaderDirectory, bool smokeTest) {
    core::Window window{1280, 720, "Emergences | Particle rendering preview"};
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
        core::ParticleRenderer particles{context, shaderDirectory};
        core::LineRenderer lines{context, shaderDirectory};
        core::ImGuiLayer ui{context, window};
        // Destroy/wait the submission owner first, even on an exception, while
        // every resource referenced by its commands is still alive.
        core::FrameRenderer renderer{context, window};
        core::Camera camera;
        Preview preview;
        auto previous = std::chrono::steady_clock::now();
        const auto deadline = previous + std::chrono::seconds{25};
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.shouldClose()) break;
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0F, 0.05F);
            previous = now;
            if (smokeTest && now >= deadline) throw std::runtime_error("Particle smoke test timed out");
            if (smokeTest) preview.antialiasing = (presentedFrames >= 2 && presentedFrames < 16) || presentedFrames >= 20;
            renderer.setAntialiasing(preview.antialiasing);
            bool fullscreen = window.isFullscreen();
            const bool presented = renderer.drawFrame(
                [&](VkRenderPass pass, std::uint32_t imageCount) {
                    particles.configure(pass, renderer.sampleCount());
                    lines.configure(pass, renderer.sampleCount());
                    ui.configure(pass, imageCount, renderer.sampleCount());
                },
                [&](VkCommandBuffer commands, VkExtent2D extent, std::size_t frame) {
                    if (smokeTest) {
                        if (presentedFrames == 10 || presentedFrames == 11 || presentedFrames == 14)
                            ImGui::GetIO().AddKeyEvent(ImGuiKey_F1, true);
                        if (presentedFrames == 12 || presentedFrames == 15)
                            ImGui::GetIO().AddKeyEvent(ImGuiKey_F1, false);
                    }
                    ui.beginFrame();
                    bool panelJustShown = false;
                    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
                        preview.panelVisible = !preview.panelVisible;
                        panelJustShown = preview.panelVisible;
                    }
                    if (smokeTest && preview.panelVisible != (presentedFrames < 10 || presentedFrames >= 14)) {
                        throw std::runtime_error("F1 must hide and restore the panel without repeating while held");
                    }
                    if (smokeTest && renderer.sampleCount() != (preview.antialiasing
                            ? renderer.supportedAntialiasingSampleCount() : VK_SAMPLE_COUNT_1_BIT)) {
                        throw std::runtime_error("Particle MSAA toggle did not select the expected sample count");
                    }
                    if (smokeTest) smokeSettings(presentedFrames, preview, camera);
                    if (preview.panelVisible) drawPanel(preview, camera, context, renderer, fullscreen);
                    if (smokeTest && presentedFrames == 16) fullscreen = true;
                    if (smokeTest && presentedFrames == 20) fullscreen = false;
                    if (!panelJustShown && (!preview.panelVisible || !ui.wantsMouse())) {
                        const auto& io = ImGui::GetIO();
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                            camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                        }
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                            camera.pan(io.MouseDelta.x, io.MouseDelta.y, io.DisplaySize.y);
                        }
                        camera.zoom(ui.wheelDelta());
                    }
                    if (preview.animate) preview.time += smokeTest ? 1.0F / 60.0F : dt;
                    preview.generate();
                    if (smokeTest && preview.count == 1) {
                        preview.particles.front().velocity = glm::vec3(0.0F);
                    }
                    core::SceneView view;
                    view.viewProjection = camera.viewProjection(static_cast<float>(extent.width) /
                                                                 static_cast<float>(extent.height));
                    view.right = glm::vec4(camera.right(), 0.0F);
                    view.up = glm::vec4(camera.up(), 0.0F);
                    view.options = {preview.scale, static_cast<float>(preview.mode), 0.0F, 0.0F};
                    ui.endFrame();
                    if (smokeTest && !preview.panelVisible && ImGui::GetDrawData()->TotalVtxCount != 0) {
                        throw std::runtime_error("Hidden controls must not leave UI draw commands");
                    }
                    lines.draw(commands, view, preview.grid, preview.bounds);
                    particles.draw(commands, frame, preview.particles, view);
                    ui.record(commands);
                });
            // Finish submitting the current frame before changing the native surface.
            if (fullscreen != window.isFullscreen()) window.setFullscreen(fullscreen);
            if (smokeTest && presented && window.isFullscreen() != (presentedFrames >= 16 && presentedFrames < 20)) {
                throw std::runtime_error("Fullscreen request did not change the window mode");
            }
            if (!presented) { window.waitEvents(); continue; }
            ++presentedFrames;
            if (smokeTest) {
                if (presentedFrames == 4) window.resize(960, 540);
                else if (presentedFrames == 8) window.resize(1280, 720);
                else if (presentedFrames >= 24 && renderer.swapchainGeneration() >= 3) break;
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
        if (presentedFrames < 24 || generations < 3) {
            throw std::runtime_error("Particle smoke test closed before all checks completed");
        }
        std::cout << "Particle smoke test passed: " << presentedFrames << " presented frames, "
                  << generations << " swapchain generations, counts 0/1/257/8192/96/2048, "
                     "arrows/dots, camera/grid/UI, MSAA/fullscreen/F1 panel, 0 validation errors.\n";
    }
    return 0;
}
} // namespace emergences::scenarios::boids
