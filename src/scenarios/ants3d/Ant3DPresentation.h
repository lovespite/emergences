#pragma once

#include <cstddef>
#include <optional>
#include <vector>
#include "config/AppConfig.h"
#include "core/MeshRenderer.h"
#include "scenarios/ants3d/Ant3DInteraction.h"

namespace emergences::core { class Camera; class FrameRenderer; }
namespace emergences::scenarios::ants3d {
class Ant3DScene;
struct Ant3DPanelActions {
    bool reset = false;
    bool step = false;
    bool skipElapsed = false;
    bool cameraChanged = false;
    bool worldReset = false;
};
void applyAnt3DSettings(Ant3DScene&, const config::Ant3DSettings&);
void captureAnt3DSettings(const Ant3DScene&, config::Ant3DSettings&);
void resetAnt3DCamera(core::Camera&);
Ant3DPanelActions drawAnt3DControls(Ant3DScene&, config::Ant3DSettings&, core::Camera&,
    const core::FrameRenderer&, bool& antialiasing,
    std::optional<std::size_t> selectedAnt = std::nullopt, bool following = false,
    FollowView followView = FollowView::FirstPerson);
void makeAnt3DColliders(const Ant3DScene&, std::vector<core::MeshVertex>&,
    std::optional<std::size_t> hiddenAnt = std::nullopt);
// Static terrain is rebuilt/uploaded only when the world or roof visibility changes.
void makeAnt3DTerrainMesh(const Ant3DScene&, const config::Ant3DSettings&, std::vector<core::MeshVertex>&);
void makeAnt3DMesh(const Ant3DScene&, const config::Ant3DSettings&, std::vector<core::MeshVertex>&,
    std::optional<std::size_t> selectedAnt = std::nullopt,
    std::optional<std::size_t> hiddenAnt = std::nullopt);
}
