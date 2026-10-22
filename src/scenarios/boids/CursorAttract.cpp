#include "scenarios/boids/CursorAttract.h"

namespace emergences::scenarios::boids {

CursorAttract::CursorAttract(float strength, float radius)
    : CursorIntervention(strength, radius, Direction::Attract) {}

} // namespace emergences::scenarios::boids
