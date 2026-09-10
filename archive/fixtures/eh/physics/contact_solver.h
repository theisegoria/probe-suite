// contact_solver.h
//
// Sequential impulse contact solver.
//
// Copyright (c) Northlight Interactive. Internal physics header.

#pragma once

#include <cstdint>

namespace nl {
namespace physics {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct RigidBody {
    Vec3  linear_velocity;
    float inverse_mass = 0.0f;   // zero means static
    float friction = 0.6f;
    float restitution = 0.2f;
};

// One persistent contact point. The accumulated impulses survive from tick to
// tick, which is what makes warm starting possible.
struct ContactPoint {
    Vec3  normal;               // unit, points from body A toward body B
    float penetration = 0.0f;   // metres of overlap, positive when overlapping
    float normal_impulse = 0.0f;   // accumulated, carried across ticks
    float tangent_impulse = 0.0f;  // accumulated, carried across ticks
    float effective_mass = 0.0f;   // cached by PrepareContact
    float bias = 0.0f;             // cached by PrepareContact
    float restitution_target = 0.0f;
};

void PrepareContact(ContactPoint& c, const RigidBody& a, const RigidBody& b,
                    float dt);

void WarmStart(ContactPoint& c, RigidBody& a, RigidBody& b);

void SolveVelocity(ContactPoint& c, RigidBody& a, RigidBody& b);

// Prepare, warm start, then run the velocity iterations.
void SolveContact(ContactPoint& c, RigidBody& a, RigidBody& b, float dt);

}  // namespace physics
}  // namespace nl
