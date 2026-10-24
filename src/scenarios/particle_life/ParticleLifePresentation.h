#pragma once

#include <string>
#include <vector>

#include "config/AppConfig.h"
#include "core/ParticleRenderer.h"

namespace emergences::core {
class Camera;
class FrameRenderer;
}

namespace emergences::scenarios::particle_life {
class ParticleLifeScene;

struct ParticleLifePanelActions {
    bool reset = false;
    bool step = false;
    bool skipElapsed = false;
};

void applyParticleLifeSettings(ParticleLifeScene&, const config::ParticleLifeSettings&);
void captureParticleLifeSettings(const ParticleLifeScene&, config::ParticleLifeSettings&);
void resetParticleLifeCamera(core::Camera&, bool threeDimensional = false);
// Draws inside the application's shared control window.
ParticleLifePanelActions drawParticleLifeControls(ParticleLifeScene&,
    config::ParticleLifeSettings&, core::Camera&, const core::FrameRenderer&,
    bool& antialiasing, bool& grid, bool& bounds, std::string& backendError);
void makeParticleLifeParticles(const ParticleLifeScene&, std::vector<core::Particle>&);

} // namespace emergences::scenarios::particle_life
