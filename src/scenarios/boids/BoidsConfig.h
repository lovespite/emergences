#pragma once

namespace emergences::config {
struct BoidsSettings;
}

namespace emergences::scenarios::boids {

class BoidsScene;

// Applies simulation parameters to the active backend, then resets the flock
// using the configured speed limits. Backend selection and cursor controls are
// owned by the interactive application.
void applyBoidsSettings(BoidsScene& scene, const config::BoidsSettings& settings);

// Reads parameters without reading back particle storage. Preserves the saved
// backend preference and cursor controls, including while GPU is unavailable.
void captureBoidsSettings(const BoidsScene& scene, config::BoidsSettings& settings);

} // namespace emergences::scenarios::boids
