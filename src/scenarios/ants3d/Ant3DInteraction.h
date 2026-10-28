#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/Camera.h"

namespace emergences::scenarios::ants3d {
class Ant3DScene;

// Pick the nearest visible body. Triangle occlusion uses the same terrain meshes
// as the renderer, including the current cave-roof cutaway setting.
[[nodiscard]] std::optional<std::size_t> pickAnt3D(
    const Ant3DScene& scene, const core::Camera::Ray& ray, bool showCaveRoofs,
    std::optional<std::size_t> hiddenAnt = std::nullopt);

enum class FollowView { FirstPerson, ThirdPerson };

class Ant3DInteraction {
public:
    [[nodiscard]] std::optional<std::size_t> selectedAnt() const noexcept { return selected_; }
    [[nodiscard]] bool following() const noexcept { return overview_.has_value(); }
    [[nodiscard]] FollowView followView() const noexcept { return followView_; }
    [[nodiscard]] bool firstPersonFollowing() const noexcept {
        return following() && followView_ == FollowView::FirstPerson;
    }
    [[nodiscard]] core::Camera::State overviewState(const core::Camera& camera) const;

    void select(std::optional<std::size_t> ant, const Ant3DScene& scene, core::Camera& camera);
    void clear(core::Camera& camera, bool restoreOverview = true);
    bool toggleFollow(const Ant3DScene& scene, core::Camera& camera);
    // F2 changes the preferred view even in overview, and changes the active
    // follow pose immediately without replacing the saved overview camera.
    void toggleFollowView(const Ant3DScene& scene, core::Camera& camera);
    void stopFollowing(core::Camera& camera, bool restoreOverview = true);
    // Zero elapsed time only validates selection/lifecycle. Advance camera
    // smoothing once per rendered frame, after stepping, using frame seconds.
    // Cooling ants retain selection but return the camera to its overview.
    void update(const Ant3DScene& scene, core::Camera& camera, double frameSeconds = 0.0);

private:
    void followSelected(const Ant3DScene& scene, core::Camera& camera, double frameSeconds);

    std::optional<std::size_t> selected_;
    std::optional<core::Camera::State> overview_;
    std::optional<std::uint64_t> generation_;
    FollowView followView_{FollowView::FirstPerson};
    float eyeHeight_{0.0F};
    float yaw_{0.0F};
    float followDistance_{0.0F};
    bool followPoseReady_{false};
};
}
