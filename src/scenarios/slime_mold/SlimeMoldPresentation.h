#pragma once

#include <string>
#include <vector>
#include "config/AppConfig.h"
#include "core/ParticleRenderer.h"

namespace emergences::core { class Camera; }
namespace emergences::scenarios::slime_mold {
class SlimeMoldScene;
struct SlimeMoldPanelActions { bool reset{}, step{}, skipElapsed{}; };
void applySlimeMoldSettings(SlimeMoldScene&, const config::SlimeMoldSettings&);
void captureSlimeMoldSettings(const SlimeMoldScene&, config::SlimeMoldSettings&);
void resetSlimeMoldCamera(core::Camera&);
SlimeMoldPanelActions drawSlimeMoldControls(SlimeMoldScene&, config::SlimeMoldSettings&,
    core::Camera&, bool& antialiasing, std::string& backendError);
void makeSlimeMoldFood(const SlimeMoldScene&, bool visible, std::vector<core::Particle>&);
} // namespace emergences::scenarios::slime_mold
