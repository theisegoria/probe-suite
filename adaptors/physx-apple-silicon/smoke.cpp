// Proof that the Apple Silicon build is not merely link-clean but runs.
// Creates a foundation and a physics instance, then exercises the vehicle
// library's sprung mass solver, which is real arithmetic through the NEON
// vecmath path this adaptor turns on.

#include <cstdio>
#include "PxPhysicsAPI.h"
#include "vehicle/PxVehicleAPI.h"

using namespace physx;

static PxDefaultAllocator gAllocator;
static PxDefaultErrorCallback gError;

int main()
{
	PxFoundation* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gError);
	if (!foundation) { std::printf("FAIL: no foundation\n"); return 1; }

	PxPhysics* physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
	if (!physics) { std::printf("FAIL: no physics\n"); return 1; }

	if (!PxInitVehicleExtension(*foundation)) { std::printf("FAIL: vehicle extension\n"); return 1; }

	// Wheel positions are given relative to the centre of mass, so an
	// asymmetric layout is what makes the solver do visible work: the front
	// axle sits 1.0 m ahead of the centre of mass and the rear 1.8 m behind,
	// which should load the front more heavily in inverse proportion to the
	// lever arms, 1.8/2.8 against 1.0/2.8.
	const PxU32 nbSprungMasses = 4;
	const PxVec3 sprungMassCoordinates[4] = {
		PxVec3(-0.8f, 0.0f,  1.0f), PxVec3(0.8f, 0.0f,  1.0f),
		PxVec3(-0.8f, 0.0f, -1.8f), PxVec3(0.8f, 0.0f, -1.8f)
	};
	const PxReal totalMass = 1500.0f;
	PxReal sprungMasses[4] = { 0, 0, 0, 0 };

	PxVehicleComputeSprungMasses(nbSprungMasses, sprungMassCoordinates,
	                             totalMass, PxVehicleAxes::eNegY, sprungMasses);

	PxReal sum = 0.0f;
	for (PxU32 i = 0; i < nbSprungMasses; ++i) sum += sprungMasses[i];

	std::printf("sprung masses: %.3f %.3f %.3f %.3f  (sum %.3f, expected %.3f)\n",
	            double(sprungMasses[0]), double(sprungMasses[1]),
	            double(sprungMasses[2]), double(sprungMasses[3]),
	            double(sum), double(totalMass));
	std::printf("front axle: %.3f kg, lever arms predict %.3f\n",
	            double(sprungMasses[0] + sprungMasses[1]), double(totalMass * (1.8f / 2.8f)));

	const PxReal front = sprungMasses[0] + sprungMasses[1];
	const PxReal expectedFront = totalMass * (1.8f / 2.8f);
	const bool massOk = PxAbs(sum - totalMass) < 1.0f;
	const bool frontLoaded = PxAbs(front - expectedFront) < 5.0f;

	PxCloseVehicleExtension();
	physics->release();
	foundation->release();

	if (!massOk)      { std::printf("FAIL: sprung masses do not sum to the total\n"); return 1; }
	if (!frontLoaded) { std::printf("FAIL: front axle carries %.1f kg, lever arms predict %.1f\n",
	                                double(front), double(expectedFront)); return 1; }
	std::printf("OK: PhysX 5 vehicle maths runs on Apple Silicon\n");
	return 0;
}
