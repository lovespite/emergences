#include "core/Camera.h"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {

using emergences::core::Camera;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.001F) {
    return glm::length(a - b) < tolerance;
}

void requireFinite(const glm::mat4& matrix) {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            require(std::isfinite(matrix[column][row]), "Camera matrix must stay finite");
        }
    }
}

glm::vec3 project(const Camera& camera, const glm::vec3& point, float aspect = 1.0F) {
    const glm::vec4 clip = camera.viewProjection(aspect) * glm::vec4(point, 1.0F);
    require(clip.w > 0.0F, "Expected point in front of camera");
    return glm::vec3(clip) / clip.w;
}

void requireBasis(const Camera& camera) {
    require(std::abs(glm::length(camera.right()) - 1.0F) < 0.0001F, "Right basis must be unit length");
    require(std::abs(glm::length(camera.up()) - 1.0F) < 0.0001F, "Up basis must be unit length");
    require(std::abs(glm::dot(camera.right(), camera.up())) < 0.0001F, "Camera basis must be orthogonal");
    requireFinite(camera.viewProjection(16.0F / 9.0F));
}

void defaultFraming() {
    Camera camera;
    requireBasis(camera);
    const glm::vec3 center = project(camera, glm::vec3(0.0F));
    require(std::abs(center.x) < 0.0001F && std::abs(center.y) < 0.0001F,
            "Default target must be centered");
    require(center.z > 0.0F && center.z < 1.0F, "Target must use Vulkan zero-to-one depth");
    for (const float aspect : {1.0F, 16.0F / 9.0F}) {
        for (const float x : {-40.0F, 40.0F}) {
            for (const float y : {-40.0F, 40.0F}) {
                for (const float z : {-40.0F, 40.0F}) {
                    const glm::vec3 ndc = project(camera, {x, y, z}, aspect);
                    require(std::abs(ndc.x) < 1.0F && std::abs(ndc.y) < 1.0F &&
                                ndc.z > 0.0F && ndc.z < 1.0F,
                            "Default world box must fit inside the viewport");
                }
            }
        }
    }
    require(project(camera, camera.up()).y < center.y, "Projection must flip Vulkan screen Y");

    // Points just ahead of the eye verify depth convention independently of the target.
    const glm::vec3 forward = glm::normalize(-camera.position());
    const glm::vec3 nearPoint = camera.position() + forward * 0.1F;
    require(std::abs(project(camera, nearPoint).z) < 0.001F, "Near plane must map to depth zero");
    const glm::vec3 farPoint = camera.position() + forward * 5000.0F;
    require(std::abs(project(camera, farPoint).z - 1.0F) < 0.0001F, "Far plane must map to depth one");
}

void orbitAndZoom() {
    Camera camera;
    const glm::vec3 initialPosition = camera.position();
    const float initialDistance = glm::length(initialPosition);
    camera.orbit(180.0F, 60.0F);
    require(!near(initialPosition, camera.position()), "Orbit must move the eye");
    require(std::abs(glm::length(camera.position()) - initialDistance) < 0.001F,
            "Orbit must preserve target distance");
    requireBasis(camera);

    const float huge = std::numeric_limits<float>::max();
    camera.orbit(huge, huge);
    requireBasis(camera);
    const float upperElevation = camera.position().y / glm::length(camera.position());
    require(upperElevation > 0.99F && upperElevation < 1.0F, "Orbit must stop short of the upper pole");
    camera.orbit(-huge, -huge);
    requireBasis(camera);
    const float lowerElevation = camera.position().y / glm::length(camera.position());
    require(lowerElevation < -0.99F && lowerElevation > -1.0F, "Orbit must stop short of the lower pole");

    camera.reset();
    camera.zoom(1.0F);
    require(glm::length(camera.position()) < initialDistance, "Positive wheel must zoom in");
    camera.zoom(-1.0F);
    require(near(camera.position(), initialPosition), "Opposite wheel steps must restore zoom");
    camera.zoom(huge);
    const glm::vec3 closest = camera.position();
    require(glm::length(closest) > 0.0F && glm::length(closest) < initialDistance,
            "Minimum zoom distance must remain positive");
    camera.zoom(huge);
    require(near(closest, camera.position()), "Zoom must saturate at the minimum distance");
    camera.zoom(-huge);
    const glm::vec3 farthest = camera.position();
    require(glm::length(farthest) > initialDistance, "Zoom out must allow a distant view");
    camera.zoom(-huge);
    require(near(farthest, camera.position()), "Zoom must saturate at the maximum distance");
    requireBasis(camera);
}

void panAndReset() {
    Camera camera;
    const glm::vec3 initialPosition = camera.position();
    const glm::vec3 initialRight = camera.right();
    const glm::vec3 initialUp = camera.up();
    camera.pan(100.0F, 50.0F, 600.0F);
    const glm::vec3 translation = camera.position() - initialPosition;
    require(glm::dot(translation, initialRight) < 0.0F, "Horizontal pan must follow scene drag");
    require(glm::dot(translation, initialUp) > 0.0F, "Vertical pan must follow scene drag");
    require(std::abs(glm::dot(translation, glm::normalize(initialPosition))) < 0.001F,
            "Pan must move in the image plane");
    require(near(camera.right(), initialRight) && near(camera.up(), initialUp),
            "Pan must preserve orientation");
    const glm::vec3 originOnScreen = project(camera, glm::vec3(0.0F));
    require(originOnScreen.x > 0.0F && originOnScreen.y > 0.0F,
            "Scene should follow right/down drag on screen");
    const glm::vec3 newTarget = project(camera, translation);
    require(std::abs(newTarget.x) < 0.0001F && std::abs(newTarget.y) < 0.0001F,
            "Pan must translate both eye and target");
    camera.orbit(80.0F, -20.0F);
    const glm::vec3 orbitCenter = project(camera, translation);
    require(std::abs(orbitCenter.x) < 0.0001F && std::abs(orbitCenter.y) < 0.0001F,
            "Orbit after pan must keep the translated target centered");
    camera.reset();
    require(near(camera.position(), initialPosition) && near(camera.right(), initialRight) &&
                near(camera.up(), initialUp), "Reset must restore framing and orientation");
}

void invalidAndExtremeInputs() {
    Camera camera;
    const glm::vec3 initialPosition = camera.position();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    camera.orbit(nan, 0.0F);
    camera.orbit(0.0F, infinity);
    camera.zoom(nan);
    camera.zoom(infinity);
    camera.pan(nan, 1.0F, 600.0F);
    camera.pan(1.0F, infinity, 600.0F);
    for (const float height : {0.0F, -1.0F, nan, infinity}) {
        camera.pan(100.0F, 50.0F, height);
    }
    require(near(camera.position(), initialPosition), "Invalid input must leave the camera unchanged");
    for (const float aspect : {0.0F, -1.0F, nan, infinity,
                               std::numeric_limits<float>::min(), std::numeric_limits<float>::max()}) {
        requireFinite(camera.viewProjection(aspect));
    }

    camera.pan(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
               std::numeric_limits<float>::min());
    camera.zoom(std::numeric_limits<float>::max());
    camera.orbit(1000.0F, 1000.0F);
    requireBasis(camera);
}

void stateRestoration() {
    Camera original;
    original.orbit(125.0F, -42.0F);
    original.pan(-95.0F, 63.0F, 720.0F);
    original.zoom(3.0F);
    const auto saved = original.state();
    Camera restored;
    restored.setState(saved);
    const auto actual = restored.state();
    require(near(actual.target, saved.target) && actual.yaw == saved.yaw &&
                actual.pitch == saved.pitch && actual.distance == saved.distance,
            "Camera state must preserve target, angles, and distance");
    require(near(restored.position(), original.position()) &&
                near(restored.right(), original.right()) && near(restored.up(), original.up()),
            "Restored camera must preserve the rendered viewpoint");
    for (const float yaw : {-std::numbers::pi_v<float>, std::numbers::pi_v<float>}) {
        auto boundary = saved;
        boundary.yaw = yaw;
        restored.setState(boundary);
        restored.setState(restored.state());
        require(restored.state().yaw == yaw, "Rounded yaw boundaries must survive repeated restoration");
    }
    restored.setState(saved);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float value : {nan, infinity, -infinity}) {
        for (int field = 0; field < 6; ++field) {
            auto invalid = saved;
            if (field < 3) invalid.target[field] = value;
            else if (field == 3) invalid.yaw = value;
            else if (field == 4) invalid.pitch = value;
            else invalid.distance = value;
            restored.setState(invalid);
            require(near(restored.position(), original.position()) &&
                        near(restored.right(), original.right()) && near(restored.up(), original.up()),
                    "A nonfinite state must leave the entire camera unchanged");
        }
    }

    const float huge = std::numeric_limits<float>::max();
    restored.setState({{huge, -huge, huge}, huge, huge, huge});
    const auto clamped = restored.state();
    require(clamped.target == glm::vec3(100000.0F, -100000.0F, 100000.0F),
            "Restored target must stay inside the pan limits");
    require(std::abs(clamped.yaw) <= 3.141593F && clamped.pitch > 1.5F && clamped.pitch < 1.57F,
            "Restored angles must be normalized and stop short of the poles");
    require(clamped.distance == 2000.0F, "Restored distance must respect the maximum zoom");
    requireBasis(restored);
    restored.setState({{}, -huge, -huge, -huge});
    require(restored.state().distance == 3.0F && restored.state().pitch > -1.57F,
            "Restored distance and pitch must respect their lower limits");
    requireBasis(restored);
}

void screenRaysAndPicking() {
    Camera camera;
    const auto centerRay = camera.screenRay(0.5F, 0.5F, 16.0F / 9.0F);
    require(centerRay.has_value(), "Center cursor must produce a ray");
    require(near(centerRay->origin, camera.position()), "Picking ray must begin at the eye");
    require(near(centerRay->direction, glm::normalize(-camera.position())),
            "Center ray must face the camera target");
    const auto cornerRay = camera.screenRay(0.0F, 0.0F, 1.0F);
    require(cornerRay.has_value(), "Viewport corner must produce a ray");
    require(glm::dot(cornerRay->direction, camera.right()) < 0.0F &&
                glm::dot(cornerRay->direction, camera.up()) > 0.0F,
            "Top-left cursor must point left and up in camera space");
    require(std::abs(glm::length(cornerRay->direction) - 1.0F) < 0.0001F,
            "Picking ray direction must be normalized");

    const glm::vec3 horizontalNormal{0.0F, 1.0F, 0.0F};
    const auto centerHit = camera.screenToPlane(0.5F, 0.5F, 1.0F, {}, horizontalNormal);
    require(centerHit && near(*centerHit, {}), "Center cursor must hit the horizontal world origin");
    for (const float scale : {std::numeric_limits<float>::min(), 5.0F,
                              std::numeric_limits<float>::max()}) {
        const auto hit = camera.screenToPlane(0.5F, 0.5F, 1.0F, {}, horizontalNormal * scale);
        require(hit && near(*hit, {}), "Plane normal scale must not affect picking");
    }

    camera.orbit(110.0F, 50.0F);
    camera.pan(80.0F, -20.0F, 720.0F);
    camera.zoom(2.0F);
    for (const float aspect : {0.5F, 1.0F, 16.0F / 9.0F}) {
        for (const glm::vec3 point : {glm::vec3{-4.0F, 0.0F, 6.0F},
                                      glm::vec3{2.0F, 0.0F, -3.0F}}) {
            const auto ndc = project(camera, point, aspect);
            const auto hit = camera.screenToPlane(
                (ndc.x + 1.0F) * 0.5F, (ndc.y + 1.0F) * 0.5F,
                aspect, {}, horizontalNormal);
            require(hit && near(*hit, point),
                    "Picking must invert the Vulkan projection after orbit, pan, and zoom");
        }
    }
    // Check the full viewport against the image plane, including clamped aspects.
    for (const float aspect : {0.001F, 0.5F, 2.0F, 200.0F}) {
        for (const float x : {0.0F, 0.5F, 1.0F}) {
            for (const float y : {0.0F, 0.5F, 1.0F}) {
                const glm::vec3 forward = glm::cross(camera.up(), camera.right());
                const auto hit = camera.screenToPlane(
                    x, y, aspect, camera.position() + forward * 10.0F, forward);
                require(hit.has_value(), "Every viewport ray must hit a plane in front of the camera");
                const auto ndc = project(camera, *hit, aspect);
                require(std::abs(ndc.x - (2.0F * x - 1.0F)) < 0.001F &&
                            std::abs(ndc.y - (2.0F * y - 1.0F)) < 0.001F,
                        "Picking and rendering must agree across the viewport");
            }
        }
    }
}

void invalidPickingInputs() {
    Camera camera;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float value : {-1.0F, 1.01F, nan, infinity}) {
        require(!camera.screenRay(value, 0.5F, 1.0F), "Invalid cursor X must be rejected");
        require(!camera.screenRay(0.5F, value, 1.0F), "Invalid cursor Y must be rejected");
    }
    for (const float aspect : {0.0F, -1.0F, nan, infinity}) {
        require(!camera.screenRay(0.5F, 0.5F, aspect), "Invalid picking aspect must be rejected");
    }
    const glm::vec3 forward = glm::normalize(-camera.position());
    require(!camera.screenToPlane(0.5F, 0.5F, 1.0F, {}, {}), "Zero plane normal must be rejected");
    require(!camera.screenToPlane(0.5F, 0.5F, 1.0F, {}, camera.right()),
            "Parallel ray must not report a hit");
    require(!camera.screenToPlane(0.5F, 0.5F, 1.0F,
                                 camera.position() - forward * 10.0F, forward),
            "Plane behind the eye must not report a hit");
    require(!camera.screenToPlane(0.5F, 0.5F, 1.0F, {nan, 0.0F, 0.0F}, forward),
            "Nonfinite plane point must be rejected");
    require(!camera.screenToPlane(0.5F, 0.5F, 1.0F, {}, {0.0F, infinity, 0.0F}),
            "Nonfinite plane normal must be rejected");
    require(!camera.screenToPlane(nan, 0.5F, 1.0F, {}, forward),
            "Invalid cursor must not report a plane hit");
    const auto eyeHit = camera.screenToPlane(0.5F, 0.5F, 1.0F, camera.position(), forward);
    require(eyeHit && near(*eyeHit, camera.position()), "A plane through the eye may hit at distance zero");
}

} // namespace

int main() {
    try {
        defaultFraming();
        orbitAndZoom();
        panAndReset();
        invalidAndExtremeInputs();
        stateRestoration();
        screenRaysAndPicking();
        invalidPickingInputs();
        std::cout << "Camera tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Camera test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
