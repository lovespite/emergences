#include "scenarios/boids/CursorRepel.h"

namespace emergences::scenarios::boids {

CursorRepel::CursorRepel(float strength, float radius)
    : CursorIntervention(strength, radius, Direction::Repel) {}

} // namespace emergences::scenarios::boids
