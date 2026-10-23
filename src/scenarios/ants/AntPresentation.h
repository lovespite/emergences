#pragma once

#include <vector>

#include "config/AppConfig.h"
#include "core/ParticleRenderer.h"

namespace emergences::core {
class Camera;
class FrameRenderer;
}

namespace emergences::scenarios::ants {
class AntScene;

struct AntPanelActions {
    bool reset = false;
    bool step = false;
    bool skipElapsed = false;
};

void applyAntSettings(AntScene&, const config::AntSettings&);
void captureAntSettings(const AntScene&, config::AntSettings&);
void resetAntCamera(core::Camera&);
// Draws inside the application's shared control window.
AntPanelActions drawAntControls(AntScene&, config::AntSettings&, core::Camera&,
    const core::FrameRenderer&, bool& antialiasing, bool& grid, bool& bounds);
void makeAntParticles(const AntScene&, const config::AntSettings&,
    std::vector<core::Particle>& ants, std::vector<core::Particle>& environment);

} // namespace emergences::scenarios::ants
