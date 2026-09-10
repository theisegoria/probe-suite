// physics/contact_rows.h
//
// Current contact row maths and its tunables.

#ifndef PHYSICS_CONTACT_ROWS_H
#define PHYSICS_CONTACT_ROWS_H

namespace physics {

struct Island;
struct ContactRow;

// Baumgarte stiffness and the penetration slop, unchanged since 2.6.
constexpr float kBaumgarteBeta = 0.2f;
constexpr float kPenetrationSlopM = 0.005f;

// Closing speed, in metres per second, at or below which a contact is treated
// as inelastic and gets no restitution at all.
//
// Raised in 2.7. The old value was low enough that a body resting on a moving
// platform picked up enough closing speed from the platform to re trigger
// restitution every few ticks, which read as a rattle. The new value covers a
// platform moving at its maximum speed.
constexpr float kRestitutionSlopMps = 1.5f;

// Bias velocity is capped so that a deeply penetrating contact cannot launch.
constexpr float kMaxBiasVelocityMps = 3.0f;

constexpr float kFrictionCombineScale = 1.0f;

void PrepareContactRows(Island& island, float dt_seconds);
void SolveContactRow(Island& island, ContactRow& row, float dt_seconds);
void SolveFrictionRow(Island& island, ContactRow& row);

void SolveContactRowsScalar(Island& island, float dt_seconds);
void SolveContactRowsWide(Island& island, float dt_seconds);
void SolvePositionRows(Island& island, float dt_seconds);

}  // namespace physics

#endif  // PHYSICS_CONTACT_ROWS_H
