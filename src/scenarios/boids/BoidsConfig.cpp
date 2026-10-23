#include "scenarios/boids/BoidsConfig.h"

#include <limits>
#include <stdexcept>

#include "config/AppConfig.h"
#include "scenarios/boids/BoidsScene.h"

namespace emergences::scenarios::boids {

void applyBoidsSettings(BoidsScene& scene, const config::BoidsSettings& settings) {
    if (settings.count < 0) {
        throw std::invalid_argument("Boids count cannot be negative");
    }
    auto& simulation = scene.currentSimulation();
    simulation.setMotionLimits({settings.minSpeed, settings.maxSpeed, settings.maxAcceleration});
    auto world = simulation.world();
    world.boundary = settings.bounce ? sim::BoundaryMode::Bounce : sim::BoundaryMode::Wrap;
    simulation.setWorld(world);
    for (const auto& rule : simulation.rules()) {
        if (!rule) {
            continue;
        }
        if (rule->name() == "Alignment") {
            rule->setWeight(settings.alignmentWeight);
            rule->setRadius(settings.alignmentRadius);
        } else if (rule->name() == "Cohesion") {
            rule->setWeight(settings.cohesionWeight);
            rule->setRadius(settings.cohesionRadius);
        } else if (rule->name() == "Separation") {
            rule->setWeight(settings.separationWeight);
            rule->setRadius(settings.separationRadius);
        }
    }
    simulation.setCount(static_cast<std::size_t>(settings.count));
    scene.setPaused(settings.paused);
    scene.reset();
}

void captureBoidsSettings(const BoidsScene& scene, config::BoidsSettings& settings) {
    const auto& simulation = scene.currentSimulation();
    if (simulation.count() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("Boids count exceeds the configuration range");
    }
    settings.count = static_cast<int>(simulation.count());
    settings.paused = scene.paused();
    const auto limits = simulation.motionLimits();
    settings.minSpeed = limits.minimumSpeed;
    settings.maxSpeed = limits.maximumSpeed;
    settings.maxAcceleration = limits.maximumAcceleration;
    settings.bounce = simulation.world().boundary == sim::BoundaryMode::Bounce;
    for (const auto& rule : simulation.rules()) {
        if (!rule) {
            continue;
        }
        if (rule->name() == "Alignment") {
            settings.alignmentWeight = rule->weight();
            settings.alignmentRadius = rule->radius();
        } else if (rule->name() == "Cohesion") {
            settings.cohesionWeight = rule->weight();
            settings.cohesionRadius = rule->radius();
        } else if (rule->name() == "Separation") {
            settings.separationWeight = rule->weight();
            settings.separationRadius = rule->radius();
        }
    }
}

} // namespace emergences::scenarios::boids
