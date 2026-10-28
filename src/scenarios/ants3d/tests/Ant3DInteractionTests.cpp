#include "scenarios/ants3d/Ant3DInteraction.h"
#include "scenarios/ants3d/Ant3DScene.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace {
using emergences::core::Camera;
using namespace emergences::scenarios::ants3d;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool sameState(const Camera::State& a, const Camera::State& b) {
    return a.target == b.target && a.yaw == b.yaw && a.pitch == b.pitch && a.distance == b.distance;
}
// Synthetic positions isolate picking and lifecycle UI boundaries without
// waiting for a randomly walking ant to visit a particular camera/test location.
void position(Ant3DScene& scene, std::size_t index, glm::vec3 value, AntActivity activity = AntActivity::Foraging) {
    auto& ant = const_cast<emergences::sim::Agent&>(scene.agents()[index]);
    ant.position = value;
    ant.velocity = {1.0F, 0.0F, 1.0F};
    const_cast<AntActivity&>(scene.activities()[index]) = activity;
}

void picking(Ant3DScene& scene) {
    position(scene, 0, {0, 100, 0});
    position(scene, 1, {0, 102, 0});
    const Camera::Ray ray{{0, 110, 0}, {0, -1, 0}};
    require(pickAnt3D(scene, ray, true) == 1, "Picking must choose the nearest visible ant");
    require(pickAnt3D(scene, ray, true, 1) == 0, "First-person picking must ignore the followed body");
    position(scene, 1, {0, 102, 0}, AntActivity::CoolingDown);
    require(pickAnt3D(scene, ray, true) == 0, "Cooling ants must not intercept picking");
    position(scene, 0, {0, 100, 0}, AntActivity::Queued);
    require(!pickAnt3D(scene, ray, true), "Queued ants must not be selectable");
    position(scene, 0, {0, 100, 0});
    require(!pickAnt3D(scene, {{0, 110, 0}, {0, 1, 0}}, true), "Ants behind the eye must not be selected");
    require(!pickAnt3D(scene, {{2, 110, 0}, {0, -1, 0}}, true), "Clicks outside the body must miss");
    require(pickAnt3D(scene, {{0, 110, 0}, {0, -2, 0}}, true) == 0,
        "Picking must normalize finite nonunit rays");
    require(!pickAnt3D(scene, {{0, 110, 0}, {0, 0, 0}}, true), "Degenerate rays must be rejected");
    require(!pickAnt3D(scene, {{0, 110, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}}, true),
        "Nonfinite rays must be rejected");

    auto chamber = scene.terrain().caveChambers().front().center;
    chamber.y = scene.terrain().navigationHeight(chamber.x, chamber.z) + Ant3DScene::AntRadius;
    position(scene, 0, chamber);
    const Camera::Ray above{chamber + glm::vec3{0, 50, 0}, {0, -1, 0}};
    require(!pickAnt3D(scene, above, true), "Opaque cave roofs must occlude underground ants");
    require(pickAnt3D(scene, above, false) == 0, "Hidden roofs must allow selecting cave ants in cutaway view");
    require(!pickAnt3D(scene, {chamber - glm::vec3{0, 50, 0}, {0, 1, 0}}, false),
        "Terrain floors must block picking from below");
}

void following(Ant3DScene& scene) {
    position(scene, 0, {0, 100, 0});
    Camera camera;
    camera.setState({{5, 3, 8}, -0.8F, 0.65F, 130.0F});
    const auto overview = camera.state();
    Ant3DInteraction interaction;
    require(!interaction.toggleFollow(scene, camera), "Space must do nothing before an ant has completed an exit");
    require(sameState(camera.state(), overview), "A missing follow candidate must preserve the camera");
    require(interaction.followView() == FollowView::FirstPerson && !interaction.firstPersonFollowing(),
        "Follow preference must default to first-person without hiding an ant in overview");
    interaction.select(0, scene, camera);
    require(interaction.toggleFollow(scene, camera), "Space must enter first-person for an active ant");
    require(interaction.following() && interaction.selectedAnt() == 0, "Following must retain the selected ID");
    require(interaction.firstPersonFollowing(), "First-person following must identify the body to hide");
    require(sameState(interaction.overviewState(camera), overview),
        "Saving settings during follow must preserve the overview instead of persisting an ant camera");
    require(glm::length(camera.position() - scene.agents()[0].position) < 0.3F,
        "First-person eye must be at the ant, not at the minimum orbit distance");
    const auto ray = camera.screenRay(0.5F, 0.5F, 1.0F);
    const auto velocity = scene.agents()[0].velocity;
    const auto heading = glm::normalize(glm::vec3{velocity.x, 0.0F, velocity.z});
    require(ray && glm::dot(ray->direction, heading) > 0.999F && std::abs(ray->direction.y) < 1.0e-6F,
        "First-person view must face the ant's horizontal heading with a level horizon");
    const auto previousEye = camera.position();
    position(scene, 0, {2, 100, 3});
    interaction.update(scene, camera, 1.0 / 60.0);
    require(glm::length(camera.position() - previousEye) > 3.0F, "The eye must move with the ant");
    const_cast<emergences::sim::Agent&>(scene.agents()[0]).velocity = {0, 0, 0};
    interaction.update(scene, camera, 1.0 / 60.0);
    require(std::isfinite(camera.position().x) && std::isfinite(camera.position().y) &&
        std::isfinite(camera.position().z), "A stationary ant must keep a finite stable heading");
    require(!interaction.toggleFollow(scene, camera), "Space again must end following");
    require(sameState(camera.state(), overview) && interaction.selectedAnt() == 0,
        "Leaving first-person must restore the overview and retain selection");
    interaction.toggleFollow(scene, camera);
    position(scene, 0, {2, 100, 3}, AntActivity::CoolingDown);
    interaction.update(scene, camera);
    require(!interaction.following() && interaction.selectedAnt() == 0 && sameState(camera.state(), overview),
        "Cooldown must restore overview while keeping the ant selected");
    require(!interaction.toggleFollow(scene, camera), "Hidden ants cannot start first-person follow");
    position(scene, 0, {2, 100, 3});
    interaction.toggleFollow(scene, camera);
    camera.reset();
    const auto preset = camera.state();
    interaction.stopFollowing(camera, false);
    require(sameState(camera.state(), preset), "A chosen camera preset must not be overwritten by restored overview");
    interaction.toggleFollow(scene, camera);
    scene.reset();
    interaction.update(scene, camera);
    require(!interaction.following() && !interaction.selectedAnt(), "World reset must clear reused ant IDs");
    require(sameState(camera.state(), preset), "World reset must restore the last overview");
    position(scene, 0, {0, 100, 0});
    interaction.select(0, scene, camera);
    scene.setCount(1);
    interaction.update(scene, camera);
    require(!interaction.selectedAnt(), "Count changes must invalidate selection even when its index still exists");
}

void followViewSwitching(Ant3DScene& scene) {
    position(scene, 0, {0, 100, 0});
    auto& ant = const_cast<emergences::sim::Agent&>(scene.agents()[0]);
    ant.velocity = {-4, 0, 0};
    Camera camera;
    const auto overview = camera.state();
    Ant3DInteraction interaction;
    interaction.toggleFollowView(scene, camera);
    require(interaction.followView() == FollowView::ThirdPerson && !interaction.following() &&
        sameState(camera.state(), overview), "F2 in overview must set the preferred follow view without moving the camera");
    interaction.select(0, scene, camera);
    require(interaction.toggleFollow(scene, camera) && !interaction.firstPersonFollowing(),
        "Third-person follow must keep its selected ant visible");
    const glm::vec3 eye = camera.position();
    require(eye.x > ant.position.x + 4.0F && eye.y > ant.position.y + 1.5F,
        "Third-person camera must trail above and behind the moving ant");
    const auto ray = camera.screenRay(0.5F, 0.5F, 1.0F);
    require(ray && glm::dot(ray->direction, glm::normalize(ant.position - eye)) > 0.999F,
        "The third-person view must keep the followed ant near the center of the screen");
    interaction.toggleFollowView(scene, camera);
    require(interaction.firstPersonFollowing() && interaction.selectedAnt() == 0 &&
        glm::length(camera.position() - ant.position) < Ant3DScene::AntRadius,
        "F2 must immediately switch the same ant into first-person");
    interaction.toggleFollowView(scene, camera);
    require(!interaction.firstPersonFollowing() && interaction.following() &&
        sameState(interaction.overviewState(camera), overview),
        "Repeated F2 switches must retain the original overview camera");
    interaction.stopFollowing(camera);
    require(sameState(camera.state(), overview) && interaction.followView() == FollowView::ThirdPerson,
        "Stopping either follow view must restore overview and retain the chosen perspective");
}

void thirdPersonObstruction(Ant3DScene& scene) {
    position(scene, 0, {0, 100, 0});
    auto& ant = const_cast<emergences::sim::Agent&>(scene.agents()[0]);
    ant.velocity = {-4, 0, 0};
    Camera camera;
    Ant3DInteraction interaction;
    interaction.toggleFollowView(scene, camera);
    interaction.select(0, scene, camera);
    interaction.toggleFollow(scene, camera);
    const float fullDistance = glm::length(camera.position() - (ant.position + glm::vec3{0, 0.12F, 0}));
    ant.position.x = Terrain::WorldHalfExtent - 2.0F;
    interaction.update(scene, camera, 1.0 / 60.0);
    const float shortenedDistance = glm::length(camera.position() - (ant.position + glm::vec3{0, 0.12F, 0}));
    require(shortenedDistance < fullDistance - 1.0F && shortenedDistance > 1.0F &&
        scene.terrain().sphereClear(camera.position(), 0.12F, 0.004F),
        "A third-person boom must retract immediately at the world boundary without clipping");
    ant.position.x = 0;
    interaction.update(scene, camera, 1.0 / 60.0);
    const float expandingDistance = glm::length(camera.position() - (ant.position + glm::vec3{0, 0.12F, 0}));
    require(expandingDistance > shortenedDistance && expandingDistance < fullDistance - 0.5F,
        "A clear third-person boom must extend smoothly after an obstruction");
    const auto pose = camera.state();
    interaction.update(scene, camera);
    interaction.update(scene, camera, std::numeric_limits<double>::infinity());
    require(sameState(camera.state(), pose), "Lifecycle checks must not advance the third-person camera");
}

void followLatestExit() {
    Ant3DScene scene(3, 173);
    Camera camera;
    Ant3DInteraction interaction;
    const auto overview = camera.state();
    scene.advance(Ant3DScene::FixedStepSeconds);
    require(!scene.latestExitedAnt() && !interaction.toggleFollow(scene, camera) && !interaction.selectedAnt() &&
        sameState(camera.state(), overview), "An ant still leaving the nest must not be the automatic follow target");
    for (int step = 0; step < 240 && !scene.latestExitedAnt(); ++step)
        scene.advance(Ant3DScene::FixedStepSeconds);
    const auto first = scene.latestExitedAnt();
    require(first && interaction.toggleFollow(scene, camera) && interaction.selectedAnt() == first,
        "Space without a selection must follow the most recent completed nest exit");
    interaction.stopFollowing(camera);
    for (int step = 0; step < 480 && scene.latestExitedAnt() == first; ++step)
        scene.advance(Ant3DScene::FixedStepSeconds);
    const auto latest = scene.latestExitedAnt();
    require(latest && latest != first, "Automatic-follow test must observe another completed exit");
    require(interaction.toggleFollow(scene, camera) && interaction.selectedAnt() == first,
        "An existing selection must take precedence over a newer nest exit");
    interaction.clear(camera);
    interaction.toggleFollowView(scene, camera);
    require(interaction.toggleFollow(scene, camera) && interaction.selectedAnt() == latest &&
        interaction.followView() == FollowView::ThirdPerson && !interaction.firstPersonFollowing(),
        "Clearing selection must let Space follow the latest exit in the preferred view");
}

void stableFollowing(Ant3DScene& scene) {
    position(scene, 0, {0, 100, 0});
    auto& ant = const_cast<emergences::sim::Agent&>(scene.agents()[0]);
    ant.velocity = {4, 0, 0};
    Camera camera;
    Ant3DInteraction interaction;
    interaction.select(0, scene, camera);
    interaction.toggleFollow(scene, camera);
    const float startingHeight = camera.position().y;
    float largestBob = 0.0F;
    for (int frame = 0; frame < 120; ++frame) {
        ant.position.y = 100.0F + (frame % 2 ? -0.06F : 0.06F);
        ant.velocity.y = frame % 2 ? -12.0F : 12.0F;
        interaction.update(scene, camera, 1.0 / 60.0);
        const auto ray = camera.screenRay(0.5F, 0.5F, 1.0F);
        require(ray && std::abs(ray->direction.y) < 1.0e-6F && camera.state().pitch == 0.0F,
            "Vertical walking/collision noise must not tilt the first-person horizon");
        largestBob = std::max(largestBob, std::abs(camera.position().y - startingHeight));
        require(glm::length(camera.position() - ant.position) < Ant3DScene::AntRadius,
            "Smoothing must keep the eye within the ant's physical clearance");
    }
    require(largestBob < 0.02F, "Eye-height smoothing must substantially reduce small vertical jitter");
    const auto pose = camera.state();
    ant.position.y += 0.05F;
    ant.velocity = {-4, 0, 0};
    interaction.update(scene, camera);
    interaction.update(scene, camera, -1.0);
    interaction.update(scene, camera, std::numeric_limits<double>::quiet_NaN());
    require(sameState(camera.state(), pose),
        "Lifecycle checks and invalid frame times must not advance smoothing a second time");
    interaction.update(scene, camera, 1.0 / 60.0);
    const auto turn = std::remainder(camera.state().yaw - pose.yaw, 2.0F * std::numbers::pi_v<float>);
    require(std::abs(turn) > 0.01F && std::abs(turn) < 0.5F,
        "A sudden reverse must turn smoothly instead of flipping the view");
    const auto stoppedYaw = camera.state().yaw;
    ant.velocity = {0.01F, 10.0F, -0.01F};
    interaction.update(scene, camera, 1.0 / 60.0);
    require(camera.state().yaw == stoppedYaw, "A waiting ant must not turn on tiny contact velocities");
}

void frameRateIndependentFollowing(Ant3DScene& scene) {
    position(scene, 0, {0, 100, 0});
    auto& ant = const_cast<emergences::sim::Agent&>(scene.agents()[0]);
    const float firstYaw = std::numbers::pi_v<float> - 0.02F;
    const float nextYaw = -std::numbers::pi_v<float> + 0.02F;
    ant.velocity = {-4.0F * std::sin(firstYaw), 0, -4.0F * std::cos(firstYaw)};
    Camera slowCamera, fastCamera;
    Ant3DInteraction slow, fast;
    slow.select(0, scene, slowCamera); fast.select(0, scene, fastCamera);
    slow.toggleFollow(scene, slowCamera); fast.toggleFollow(scene, fastCamera);
    ant.position.y += 0.04F;
    ant.velocity = {-4.0F * std::sin(nextYaw), 0, -4.0F * std::cos(nextYaw)};
    slow.update(scene, slowCamera, 1.0 / 30.0);
    fast.update(scene, fastCamera, 1.0 / 60.0);
    fast.update(scene, fastCamera, 1.0 / 60.0);
    require(glm::length(slowCamera.position() - fastCamera.position()) < 0.00002F &&
        std::abs(slowCamera.state().yaw - fastCamera.state().yaw) < 0.00001F,
        "Equal elapsed time at 30 and 60 FPS must produce the same smoothed pose");
    const float turn = std::remainder(slowCamera.state().yaw - firstYaw, 2.0F * std::numbers::pi_v<float>);
    require(turn > 0.0F && turn < 0.02F, "Yaw smoothing must cross +/-pi along the short path");
    for (int frame = 0; frame < 120; ++frame) fast.update(scene, fastCamera, 1.0 / 60.0);
    require(std::abs(fastCamera.position().y - (ant.position.y + 0.12F)) < 0.001F &&
        std::abs(std::remainder(fastCamera.state().yaw - nextYaw, 2.0F * std::numbers::pi_v<float>)) < 0.001F,
        "A paused ant's view must settle at its actual height and heading without residual drift");
}

void followedTerrainClearance(bool thirdPerson) {
    Ant3DScene scene(1, 173);
    for (int step = 0; step < 120 && scene.activities()[0] != AntActivity::Foraging; ++step)
        scene.advance(Ant3DScene::FixedStepSeconds);
    require(scene.activities()[0] == AntActivity::Foraging, "The camera fixture must begin after a completed exit");
    // Place the active body on the cave floor near its initial food patch.
    // Local discovery then drives a real collection and return traversal;
    // camera coverage must not depend on random exploration finding this cave.
    auto chamber = scene.terrain().caveChambers().front().center;
    chamber.y = scene.terrain().navigationHeight(chamber.x, chamber.z) + Ant3DScene::AntRadius;
    chamber.y = scene.terrain().walkingHeight(chamber, Ant3DScene::AntRadius);
    require(scene.terrain().insideCave(chamber) && scene.terrain().sphereClear(chamber, Ant3DScene::AntRadius),
        "The follow fixture must start on a physically valid cave floor");
    position(scene, 0, chamber);
    Camera camera;
    Ant3DInteraction interaction;
    if (thirdPerson) interaction.toggleFollowView(scene, camera);
    interaction.select(0, scene, camera);
    interaction.toggleFollow(scene, camera);
    int caveFrames = 0;
    float walkedDistance = 0.0F;
    auto previousPosition = scene.agents()[0].position;
    bool outdoorSeen = false;
    for (int frame = 0; frame < 1200 && interaction.following(); ++frame) {
        scene.advance(4.0 * Ant3DScene::FixedStepSeconds);
        interaction.update(scene, camera, 1.0 / 30.0);
        if (!interaction.following()) break;
        walkedDistance += glm::length(scene.agents()[0].position - previousPosition);
        previousPosition = scene.agents()[0].position;
        require(scene.terrain().sphereClear(camera.position(), thirdPerson ? 0.10F : 0.06F, 0.012F),
            "Both follow cameras must stay clear of the ground and cave ceilings");
        if (!thirdPerson)
            require(camera.state().pitch == 0.0F, "Slopes and cave traversal must retain a level horizon");
        else {
            const auto body = scene.agents()[0].position;
            const auto eye = camera.position();
            const float horizontalDistance = std::hypot(eye.x - body.x, eye.z - body.z);
            const glm::vec3 anchor{body.x, eye.y - std::tan(camera.state().pitch) * horizontalDistance, body.z};
            // Intermediate samples catch a boom that crosses solid rock even
            // when its final camera position happens to be clear.
            for (int sample = 1; sample < 8; ++sample) {
                const glm::vec3 point = anchor + (camera.position() - anchor) * (sample / 8.0F);
                require(scene.terrain().sphereClear(point, 0.06F, 0.02F),
                    "The third-person line of sight must stay on the ant's side of cave walls");
            }
        }
        if (scene.terrain().insideCave(scene.agents()[0].position)) ++caveFrames;
        else outdoorSeen = true;
    }
    require(caveFrames >= 10 && walkedDistance > 5.0F && outdoorSeen,
        "Follow clearance must exercise actual cave movement and the transition back outdoors");
    require(scene.deliveredFood() == 1,
        "The cave fixture must discover nearby food and complete its return traversal");
}
}

int main() {
    try {
        Ant3DScene scene(2);
        picking(scene);
        following(scene);
        followViewSwitching(scene);
        thirdPersonObstruction(scene);
        stableFollowing(scene);
        frameRateIndependentFollowing(scene);
        followLatestExit();
        followedTerrainClearance(false);
        followedTerrainClearance(true);
        std::cout << "3D ant picking, first/third-person cameras, latest-exit following, clearance, and lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
