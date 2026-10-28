#include "scenarios/ants3d/Ant3DInteraction.h"
#include "scenarios/ants3d/Ant3DScene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include <glm/geometric.hpp>

namespace emergences::scenarios::ants3d {
namespace {
bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::optional<float> bodyHit(const core::Camera::Ray& ray, glm::vec3 center) {
    // Double precision keeps a sub-unit body pickable from distant overviews.
    const glm::dvec3 relative = glm::dvec3(ray.origin) - glm::dvec3(center);
    const glm::dvec3 direction(ray.direction);
    const double a = glm::dot(direction, direction);
    const double b = glm::dot(relative, direction);
    const double c = glm::dot(relative, relative) -
        static_cast<double>(Ant3DScene::AntRadius) * Ant3DScene::AntRadius;
    const double discriminant = b * b - a * c;
    if (discriminant < 0.0F) return std::nullopt;
    const double far = (-b + std::sqrt(discriminant)) / a;
    if (far < 0.0F) return std::nullopt;
    return static_cast<float>(std::max(0.0, (-b - std::sqrt(discriminant)) / a));
}

bool meshOccludes(std::span<const TerrainVertex> mesh, const core::Camera::Ray& ray, float limit) {
    for (std::size_t i = 0; i + 2 < mesh.size(); i += 3) {
        const glm::vec3 a = mesh[i].position;
        const glm::vec3 edge1 = mesh[i + 1].position - a;
        const glm::vec3 edge2 = mesh[i + 2].position - a;
        const glm::vec3 p = glm::cross(ray.direction, edge2);
        const float determinant = glm::dot(edge1, p);
        // The terrain is solid from either side, including chamber ceilings.
        if (std::abs(determinant) < 1.0e-7F) continue;
        const float inverse = 1.0F / determinant;
        const glm::vec3 relative = ray.origin - a;
        const float u = glm::dot(relative, p) * inverse;
        if (u < 0.0F || u > 1.0F) continue;
        const glm::vec3 q = glm::cross(relative, edge1);
        const float v = glm::dot(ray.direction, q) * inverse;
        if (v < 0.0F || u + v > 1.0F) continue;
        const float distance = glm::dot(edge2, q) * inverse;
        if (distance > 0.001F && distance <= limit + 0.002F) return true;
    }
    return false;
}

float clearFollowDistance(const Terrain& terrain, glm::vec3 anchor, glm::vec3 direction) {
    constexpr float MaximumDistance = 4.5F;
    constexpr float CameraRadius = 0.12F;
    constexpr float SampleDistance = 0.08F;
    // Sweep outward from the ant, rather than testing only the destination:
    // the camera must not pass through a wall or roof to reach clear space.
    float clear = 0.0F;
    for (float distance = SampleDistance; ; distance += SampleDistance) {
        const float sample = std::min(distance, MaximumDistance);
        if (!terrain.sphereClear(anchor + direction * sample, CameraRadius)) {
            float blocked = sample;
            for (int iteration = 0; iteration < 7; ++iteration) {
                const float midpoint = (clear + blocked) * 0.5F;
                if (terrain.sphereClear(anchor + direction * midpoint, CameraRadius)) clear = midpoint;
                else blocked = midpoint;
            }
            return clear;
        }
        clear = sample;
        if (sample == MaximumDistance) return clear;
    }
}
}

std::optional<std::size_t> pickAnt3D(const Ant3DScene& scene, const core::Camera::Ray& inputRay,
    bool showCaveRoofs, std::optional<std::size_t> hiddenAnt) {
    if (!finite(inputRay.origin) || !finite(inputRay.direction)) return std::nullopt;
    const float directionLength = glm::length(inputRay.direction);
    if (!std::isfinite(directionLength) || directionLength < 1.0e-6F) return std::nullopt;
    const core::Camera::Ray ray{inputRay.origin, inputRay.direction / directionLength};
    float nearest = std::numeric_limits<float>::infinity();
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < scene.count(); ++i) {
        if (!scene.active(i) || hiddenAnt == i) continue;
        const auto hit = bodyHit(ray, scene.agents()[i].position);
        if (hit && *hit < nearest) { nearest = *hit; selected = i; }
    }
    if (!selected) return std::nullopt;
    const auto& terrain = scene.terrain();
    if (meshOccludes(terrain.mesh(), ray, nearest) ||
        meshOccludes(terrain.featureMesh(), ray, nearest) ||
        (showCaveRoofs && meshOccludes(terrain.roofMesh(), ray, nearest))) return std::nullopt;
    return selected;
}

core::Camera::State Ant3DInteraction::overviewState(const core::Camera& camera) const {
    return overview_.value_or(camera.state());
}

void Ant3DInteraction::select(std::optional<std::size_t> ant, const Ant3DScene& scene,
    core::Camera& camera) {
    update(scene, camera);
    stopFollowing(camera);
    selected_ = ant && *ant < scene.count() && scene.active(*ant) ? ant : std::nullopt;
}

void Ant3DInteraction::clear(core::Camera& camera, bool restoreOverview) {
    stopFollowing(camera, restoreOverview);
    selected_.reset();
}

void Ant3DInteraction::stopFollowing(core::Camera& camera, bool restoreOverview) {
    if (overview_ && restoreOverview) camera.setState(*overview_);
    overview_.reset();
    followPoseReady_ = false;
}

bool Ant3DInteraction::toggleFollow(const Ant3DScene& scene, core::Camera& camera) {
    update(scene, camera);
    if (following()) { stopFollowing(camera); return false; }
    if (!selected_) selected_ = scene.latestExitedAnt();
    if (!selected_ || !scene.active(*selected_)) return false;
    overview_ = camera.state();
    followSelected(scene, camera, 0.0);
    return true;
}

void Ant3DInteraction::toggleFollowView(const Ant3DScene& scene, core::Camera& camera) {
    update(scene, camera);
    followView_ = followView_ == FollowView::FirstPerson ? FollowView::ThirdPerson : FollowView::FirstPerson;
    if (!following()) return;
    followPoseReady_ = false;
    followSelected(scene, camera, 0.0);
}

void Ant3DInteraction::update(const Ant3DScene& scene, core::Camera& camera, double frameSeconds) {
    if (generation_ && *generation_ != scene.generation()) clear(camera);
    generation_ = scene.generation();
    if (selected_ && *selected_ >= scene.count()) clear(camera);
    if (!following()) return;
    if (!selected_ || !scene.active(*selected_)) { stopFollowing(camera); return; }
    followSelected(scene, camera, frameSeconds);
}

void Ant3DInteraction::followSelected(const Ant3DScene& scene, core::Camera& camera, double frameSeconds) {
    const auto& ant = scene.agents()[*selected_];
    const double dt = std::isfinite(frameSeconds) ? std::clamp(frameSeconds, 0.0, 0.1) : 0.0;
    if (followPoseReady_ && dt == 0.0) return;
    const bool initializePose = !followPoseReady_;
    // Ground triangles and contact corrections can change the local normal
    // abruptly. Keep a level horizon and turn using horizontal motion only.
    float targetYaw = followPoseReady_ ? yaw_ : camera.state().yaw;
    if (ant.velocity.x * ant.velocity.x + ant.velocity.z * ant.velocity.z > 0.15F * 0.15F)
        targetYaw = std::atan2(-ant.velocity.x, -ant.velocity.z);
    const float targetHeight = ant.position.y + 0.12F;
    if (!followPoseReady_) {
        eyeHeight_ = targetHeight;
        yaw_ = targetYaw;
        followPoseReady_ = true;
    } else {
        // Time-based filters behave consistently across render rates. Unwrap
        // yaw before filtering so crossing +/-pi never spins the long way.
        eyeHeight_ += (targetHeight - eyeHeight_) * static_cast<float>(-std::expm1(-dt / 0.18));
        const double turn = std::remainder(static_cast<double>(targetYaw) - yaw_, 2.0 * std::numbers::pi);
        yaw_ = static_cast<float>(std::remainder(yaw_ + turn * -std::expm1(-dt / 0.14),
                                               2.0 * std::numbers::pi));
    }
    // Follow X/Z without lag and keep the eye inside the ant's terrain-clear
    // collision sphere, including under cave roofs. Only vertical bob is damped.
    eyeHeight_ = std::clamp(eyeHeight_, ant.position.y - 0.08F, ant.position.y + 0.20F);
    const glm::vec3 eye{ant.position.x, eyeHeight_, ant.position.z};
    const glm::vec3 forward{-std::sin(yaw_), 0.0F, -std::cos(yaw_)};
    if (followView_ == FollowView::FirstPerson) {
        camera.setState({eye + forward * 4.0F, yaw_, 0.0F, 4.0F});
        return;
    }
    constexpr float Pitch = 0.35F;
    const glm::vec3 boom = -forward * std::cos(Pitch) + glm::vec3{0, std::sin(Pitch), 0};
    const float availableDistance = clearFollowDistance(scene.terrain(), eye, boom);
    // Retract immediately to avoid clipping, then ease back out when the view
    // clears. Heading and height already use the same time-based follow filters.
    followDistance_ = initializePose ? availableDistance : std::min(availableDistance,
        followDistance_ + (availableDistance - followDistance_) * static_cast<float>(-std::expm1(-dt / 0.20)));
    const glm::vec3 trailingEye = eye + boom * followDistance_;
    // Preserve the actual eye even for a shortened boom below Camera's minimum
    // orbit distance. The selected ant still lies along the center viewing ray.
    camera.setState({trailingEye - boom * 4.0F, yaw_, Pitch, 4.0F});
}
}
