#include "core/Camera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace emergences::core {
namespace {

constexpr float fieldOfView = std::numbers::pi_v<float> / 4.0F;
constexpr float minimumDistance = 3.0F;
constexpr float maximumDistance = 2000.0F;
constexpr double maximumPitch = std::numbers::pi / 2.0 - 0.01;
constexpr double orbitRadiansPerPixel = 0.005;
constexpr double maximumTargetCoordinate = 100000.0;

bool finiteDeltas(float x, float y) {
    return std::isfinite(x) && std::isfinite(y);
}

bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

Camera::Camera() { reset(); }

Camera::State Camera::state() const {
    return {target_, yaw_, pitch_, distance_};
}

void Camera::setState(const State& state) {
    if (!finiteVector(state.target) || !finiteDeltas(state.yaw, state.pitch) ||
        !std::isfinite(state.distance)) {
        return;
    }
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        target_[axis] = static_cast<float>(std::clamp(
            static_cast<double>(state.target[axis]),
            -maximumTargetCoordinate, maximumTargetCoordinate));
    }
    // Preserve the rounded float endpoints that orbit() can produce; reducing
    // float(pi) in doubles would otherwise flip its sign on every restore.
    yaw_ = std::abs(state.yaw) <= std::numbers::pi_v<float> ? state.yaw :
        static_cast<float>(std::remainder(static_cast<double>(state.yaw), 2.0 * std::numbers::pi));
    pitch_ = static_cast<float>(std::clamp(
        static_cast<double>(state.pitch), -maximumPitch, maximumPitch));
    distance_ = std::clamp(state.distance, minimumDistance, maximumDistance);
}

glm::vec3 Camera::right() const {
    return {std::cos(yaw_), 0.0F, -std::sin(yaw_)};
}

glm::vec3 Camera::up() const {
    return {-std::sin(pitch_) * std::sin(yaw_), std::cos(pitch_),
            -std::sin(pitch_) * std::cos(yaw_)};
}

glm::vec3 Camera::position() const {
    return target_ + distance_ * glm::vec3{std::cos(pitch_) * std::sin(yaw_),
                                         std::sin(pitch_),
                                         std::cos(pitch_) * std::cos(yaw_)};
}

glm::mat4 Camera::viewProjection(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0.0F) {
        aspect = 1.0F;
    }
    // Extremely narrow/tall transient resize values must not overflow projection.
    aspect = std::clamp(aspect, 0.01F, 100.0F);
    auto projection = glm::perspectiveRH_ZO(fieldOfView, aspect, 0.1F, 5000.0F);
    projection[1][1] *= -1.0F;
    return projection * glm::lookAtRH(position(), target_, up());
}

std::optional<Camera::Ray> Camera::screenRay(
    float normalizedX, float normalizedY, float aspect) const {
    if (!finiteDeltas(normalizedX, normalizedY) ||
        normalizedX < 0.0F || normalizedX > 1.0F ||
        normalizedY < 0.0F || normalizedY > 1.0F ||
        !std::isfinite(aspect) || aspect <= 0.0F) {
        return std::nullopt;
    }
    aspect = std::clamp(aspect, 0.01F, 100.0F);
    const float halfHeight = std::tan(fieldOfView / 2.0F);
    const glm::vec3 forward = glm::cross(up(), right());
    const glm::vec3 direction = forward +
        right() * ((2.0F * normalizedX - 1.0F) * halfHeight * aspect) +
        up() * ((1.0F - 2.0F * normalizedY) * halfHeight);
    return Ray{position(), glm::normalize(direction)};
}

std::optional<glm::vec3> Camera::screenToPlane(
    float normalizedX, float normalizedY, float aspect,
    const glm::vec3& planePoint, const glm::vec3& planeNormal) const {
    const auto ray = screenRay(normalizedX, normalizedY, aspect);
    if (!ray || !finiteVector(planePoint) || !finiteVector(planeNormal)) {
        return std::nullopt;
    }
    // Doubles keep both tiny and huge finite plane normals usable.
    const glm::dvec3 normal(planeNormal);
    const double normalLength = glm::length(normal);
    if (normalLength == 0.0) {
        return std::nullopt;
    }
    const glm::dvec3 unitNormal = normal / normalLength;
    const glm::dvec3 direction(ray->direction);
    const double denominator = glm::dot(direction, unitNormal);
    if (std::abs(denominator) < 1.0e-6) {
        return std::nullopt;
    }
    const glm::dvec3 origin(ray->origin);
    const double distance = glm::dot(glm::dvec3(planePoint) - origin, unitNormal) / denominator;
    if (!std::isfinite(distance) || distance < 0.0) {
        return std::nullopt;
    }
    const glm::dvec3 hit = origin + direction * distance;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(hit[axis]) || std::abs(hit[axis]) > std::numeric_limits<float>::max()) {
            return std::nullopt;
        }
    }
    return glm::vec3(hit);
}

void Camera::orbit(float deltaX, float deltaY) {
    if (!finiteDeltas(deltaX, deltaY)) {
        return;
    }
    // Use doubles before reducing huge deltas so finite input cannot overflow.
    yaw_ = static_cast<float>(std::remainder(
        static_cast<double>(yaw_) - static_cast<double>(deltaX) * orbitRadiansPerPixel,
        2.0 * std::numbers::pi));
    pitch_ = static_cast<float>(std::clamp(
        static_cast<double>(pitch_) + static_cast<double>(deltaY) * orbitRadiansPerPixel,
        -maximumPitch, maximumPitch));
}

void Camera::pan(float deltaX, float deltaY, float viewportHeight) {
    if (!finiteDeltas(deltaX, deltaY) || !std::isfinite(viewportHeight) || viewportHeight <= 0.0F) {
        return;
    }
    const double unitsPerPixel =
        2.0 * distance_ * std::tan(static_cast<double>(fieldOfView) / 2.0) / viewportHeight;
    const glm::dvec3 offset = unitsPerPixel *
        (-static_cast<double>(deltaX) * glm::dvec3(right()) +
         static_cast<double>(deltaY) * glm::dvec3(up()));
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        target_[axis] = static_cast<float>(std::clamp(
            static_cast<double>(target_[axis]) + offset[axis],
            -maximumTargetCoordinate, maximumTargetCoordinate));
    }
}

void Camera::look(float deltaX, float deltaY) {
    if (!finiteDeltas(deltaX, deltaY) || (deltaX == 0.0F && deltaY == 0.0F)) {
        return;
    }
    const glm::vec3 eye = position();
    const float previousYaw = yaw_;
    const float previousPitch = pitch_;
    orbit(deltaX, deltaY);
    const glm::vec3 offset = distance_ * glm::vec3{
        std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
        std::cos(pitch_) * std::cos(yaw_)};
    const glm::vec3 target = eye - offset;
    // At the world-coordinate limit, reject the look instead of clamping the
    // target and unexpectedly moving the eye. Normal exploration is unaffected.
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (std::abs(static_cast<double>(target[axis])) > maximumTargetCoordinate) {
            yaw_ = previousYaw;
            pitch_ = previousPitch;
            return;
        }
    }
    target_ = target;
}

void Camera::move(float rightDistance, float upDistance, float forwardDistance) {
    if (!finiteDeltas(rightDistance, forwardDistance) || !std::isfinite(upDistance)) {
        return;
    }
    const glm::dvec3 forward{-std::sin(yaw_), 0.0, -std::cos(yaw_)};
    const glm::dvec3 offset = static_cast<double>(rightDistance) * glm::dvec3(right()) +
        glm::dvec3{0.0, upDistance, 0.0} + static_cast<double>(forwardDistance) * forward;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        target_[axis] = static_cast<float>(std::clamp(
            static_cast<double>(target_[axis]) + offset[axis],
            -maximumTargetCoordinate, maximumTargetCoordinate));
    }
}

void Camera::zoom(float wheelDelta) {
    if (!std::isfinite(wheelDelta)) {
        return;
    }
    // Bounding the exponent avoids overflow even for synthetic input events.
    const double exponent = std::clamp(-static_cast<double>(wheelDelta) * 0.12, -20.0, 20.0);
    distance_ = static_cast<float>(std::clamp(
        static_cast<double>(distance_) * std::exp(exponent),
        static_cast<double>(minimumDistance), static_cast<double>(maximumDistance)));
}

void Camera::reset() {
    target_ = glm::vec3(0.0F);
    yaw_ = std::numbers::pi_v<float> / 4.0F;
    pitch_ = std::numbers::pi_v<float> / 6.0F;
    distance_ = 220.0F;
}

} // namespace emergences::core
