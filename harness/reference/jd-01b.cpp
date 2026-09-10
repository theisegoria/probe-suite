// A second reference for jd-01, sharing as little as possible with the
// first. Its job is the opposite of a mutant's: to show the ladder accepts
// a good solution that is not the one it was tuned against.
//
// Different from jd-01.cpp in every part that could have been overfitted:
//   wheel indices  front is 2 and 3, rear is 0 and 1. The first reference
//                  has it the other way round, so anything that quietly
//                  assumed wheel 0 was front left will show up here.
//   controller     pure pursuit against a lookahead point on the waypoint
//                  polyline, rather than heading error to the next waypoint
//   speed          a PI controller on speed error, not a proportional one
//   suspension     stiffer and more damped, still inside the spec
//   drivetrain     different gear ratios and a different torque curve
//   output         rows accumulated and written at the end, not streamed
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "PxPhysicsAPI.h"
#include "vehicle/PxVehicleAPI.h"

#include "base/Base.h"
#include "enginedrivetrain/EngineDrivetrain.h"
#include "physxintegration/PhysXIntegration.h"

using namespace physx;
using namespace snippetvehicle;

static PxDefaultAllocator gAllocator;
static PxDefaultErrorCallback gErrorCallback;
static PxFoundation* gFoundation = nullptr;
static PxPhysics* gPhysics = nullptr;
static PxDefaultCpuDispatcher* gDispatcher = nullptr;
static PxScene* gScene = nullptr;
static PxMaterial* gMaterial = nullptr;
static EngineDriveVehicle gVehicle;
static PxVehiclePhysXSimulationContext gCtx;
static const PxVec3 gGravity(0.0f, -9.81f, 0.0f);

// Front is 2 and 3 here. Everything downstream reads these, so the layout
// is stated once rather than spelled out at each use.
static const PxU32 FL = 2, FR = 3, RL = 0, RR = 1;

// ------------------------------------------------------------------ assets

static bool loadObj(const char* path, std::vector<PxVec3>& verts, std::vector<PxU32>& idx) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2 || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            verts.push_back(PxVec3(x, y, z));
        } else if (tag == "f") {
            std::vector<int> f;
            std::string tok;
            while (ss >> tok) {
                const std::size_t slash = tok.find('/');
                if (slash != std::string::npos) tok = tok.substr(0, slash);
                const int i = std::stoi(tok);
                f.push_back(i > 0 ? i - 1 : int(verts.size()) + i);
            }
            for (std::size_t k = 1; k + 1 < f.size(); ++k) {
                idx.push_back(PxU32(f[0]));
                idx.push_back(PxU32(f[k]));
                idx.push_back(PxU32(f[k + 1]));
            }
        }
    }
    return !verts.empty() && !idx.empty();
}

static bool initTerrain(const PxCookingParams& cooking) {
    std::vector<PxVec3> verts;
    std::vector<PxU32> idx;
    if (!loadObj("assets/terrain.obj", verts, idx)) return false;

    PxTriangleMeshDesc desc;
    desc.points.count = PxU32(verts.size());
    desc.points.stride = sizeof(PxVec3);
    desc.points.data = verts.data();
    desc.triangles.count = PxU32(idx.size() / 3);
    desc.triangles.stride = 3 * sizeof(PxU32);
    desc.triangles.data = idx.data();

    PxDefaultMemoryOutputStream buf;
    if (!PxCookTriangleMesh(cooking, desc, buf)) return false;
    PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
    PxTriangleMesh* mesh = gPhysics->createTriangleMesh(input);
    if (!mesh) return false;

    PxRigidStatic* ground = gPhysics->createRigidStatic(PxTransform(PxIdentity));
    PxRigidActorExt::createExclusiveShape(*ground, PxTriangleMeshGeometry(mesh), *gMaterial);
    gScene->addActor(*ground);
    mesh->release();
    return true;
}

// -------------------------------------------------------------- parameters

static void setBaseParams(BaseVehicleParams& p) {
    p.axleDescription.setToDefault();
    const PxU32 front[2] = {FL, FR};
    const PxU32 rear[2] = {RL, RR};
    p.axleDescription.addAxle(2, front);
    p.axleDescription.addAxle(2, rear);

    p.frame.lngAxis = PxVehicleAxes::ePosZ;
    p.frame.latAxis = PxVehicleAxes::ePosX;
    p.frame.vrtAxis = PxVehicleAxes::ePosY;
    p.scale.scale = 1.0f;

    p.suspensionStateCalculationParams.suspensionJounceCalculationType =
        PxVehicleSuspensionJounceCalculationType::eSWEEP;
    p.suspensionStateCalculationParams.limitSuspensionExpansionVelocity = false;

    p.brakeResponseParams[0].maxResponse = 3600.0f;
    p.brakeResponseParams[1].maxResponse = 0.0f;
    for (PxU32 i = 0; i < 4; ++i) {
        p.brakeResponseParams[0].wheelResponseMultipliers[i] = 1.0f;
        p.brakeResponseParams[1].wheelResponseMultipliers[i] = 0.0f;
    }

    const float maxSteer = 35.0f * PxPi / 180.0f;
    p.steerResponseParams.maxResponse = maxSteer;
    p.steerResponseParams.wheelResponseMultipliers[FL] = 1.0f;
    p.steerResponseParams.wheelResponseMultipliers[FR] = 1.0f;
    p.steerResponseParams.wheelResponseMultipliers[RL] = 0.0f;
    p.steerResponseParams.wheelResponseMultipliers[RR] = 0.0f;

    p.ackermannParams[0].wheelIds[0] = FL;
    p.ackermannParams[0].wheelIds[1] = FR;
    p.ackermannParams[0].wheelBase = 2.70f;
    p.ackermannParams[0].trackWidth = 1.72f;
    p.ackermannParams[0].strength = 1.0f;

    // Wheel centres come from the spec. The strut mounts above them by
    // travel * 2/3, so the vehicle rests a third into its travel.
    const float travel = 0.25f;
    const float restOffset = travel * 2.0f / 3.0f;
    PxVec3 wheelCentre[4];
    wheelCentre[FL] = PxVec3(-0.86f, -0.52f,  1.35f);
    wheelCentre[FR] = PxVec3( 0.86f, -0.52f,  1.35f);
    wheelCentre[RL] = PxVec3(-0.86f, -0.52f, -1.35f);
    wheelCentre[RR] = PxVec3( 0.86f, -0.52f, -1.35f);

    const float sprung = 1500.0f / 4.0f;
    // Stiffer than the first reference and damped closer to critical.
    const float stiffness = sprung * 9.81f / (travel / 3.0f) * 1.25f;
    const float damping = 1.6f * std::sqrt(stiffness * sprung);

    for (PxU32 i = 0; i < 4; ++i) {
        const PxVec3 mount = wheelCentre[i] + PxVec3(0.0f, restOffset, 0.0f);
        p.suspensionParams[i].suspensionAttachment = PxTransform(mount, PxQuat(PxIdentity));
        p.suspensionParams[i].suspensionTravelDir = PxVec3(0.0f, -1.0f, 0.0f);
        p.suspensionParams[i].suspensionTravelDist = travel;
        p.suspensionParams[i].wheelAttachment = PxTransform(PxIdentity);

        p.suspensionComplianceParams[i].wheelToeAngle.addPair(0.0f, 0.0f);
        p.suspensionComplianceParams[i].wheelCamberAngle.addPair(0.0f, 0.0f);
        p.suspensionComplianceParams[i].suspForceAppPoint.addPair(0.0f, PxVec3(0.0f, -0.09f, 0.0f));
        p.suspensionComplianceParams[i].tireForceAppPoint.addPair(0.0f, PxVec3(0.0f, -0.09f, 0.0f));

        p.suspensionForceParams[i].stiffness = stiffness;
        p.suspensionForceParams[i].damping = damping;
        p.suspensionForceParams[i].sprungMass = sprung;

        p.tireForceParams[i].longStiff = 24000.0f;
        p.tireForceParams[i].latStiffX = 0.02f;
        p.tireForceParams[i].latStiffY = 95000.0f;
        p.tireForceParams[i].camberStiff = 0.0f;
        p.tireForceParams[i].restLoad = sprung * 9.81f;
        p.tireForceParams[i].frictionVsSlip[0][0] = 0.0f;
        p.tireForceParams[i].frictionVsSlip[0][1] = 1.0f;
        p.tireForceParams[i].frictionVsSlip[1][0] = 0.15f;
        p.tireForceParams[i].frictionVsSlip[1][1] = 1.0f;
        p.tireForceParams[i].frictionVsSlip[2][0] = 1.0f;
        p.tireForceParams[i].frictionVsSlip[2][1] = 1.0f;
        p.tireForceParams[i].loadFilter[0][0] = 0.0f;
        p.tireForceParams[i].loadFilter[0][1] = 0.25f;
        p.tireForceParams[i].loadFilter[1][0] = 3.0f;
        p.tireForceParams[i].loadFilter[1][1] = 3.0f;

        p.wheelParams[i].radius = 0.36f;
        p.wheelParams[i].halfWidth = 0.15f;
        p.wheelParams[i].mass = 20.0f;
        p.wheelParams[i].moi = 0.5f * 20.0f * 0.36f * 0.36f;
        p.wheelParams[i].dampingRate = 0.20f;
    }

    p.rigidBodyParams.mass = 1500.0f;
    p.rigidBodyParams.moi = PxVec3(1885.0f, 2210.0f, 485.0f);
}

static void setEngineParams(EngineDrivetrainParams& e) {
    e.autoboxParams.latency = 1.6f;
    for (PxU32 i = 0; i < PxVehicleGearboxParams::eMAX_NB_GEARS; ++i) {
        e.autoboxParams.upRatios[i] = 0.70f;
        e.autoboxParams.downRatios[i] = 0.45f;
    }
    e.autoboxParams.upRatios[1] = 0.15f;                  // neutral

    e.clutchCommandResponseParams.maxResponse = 10.0f;

    // A shaped curve rather than a flat one, so peak torque sits mid range.
    e.engineParams.torqueCurve.addPair(0.0f, 0.80f);
    e.engineParams.torqueCurve.addPair(0.45f, 1.00f);
    e.engineParams.torqueCurve.addPair(1.0f, 0.72f);
    e.engineParams.moi = 1.2f;
    e.engineParams.peakTorque = 600.0f;
    e.engineParams.idleOmega = 0.0f;
    e.engineParams.maxOmega = 620.0f;
    e.engineParams.dampingRateFullThrottle = 0.15f;
    e.engineParams.dampingRateZeroThrottleClutchEngaged = 2.0f;
    e.engineParams.dampingRateZeroThrottleClutchDisengaged = 0.35f;

    e.gearBoxParams.neutralGear = 1;
    e.gearBoxParams.ratios[0] = -3.6f;
    e.gearBoxParams.ratios[1] = 0.0f;
    e.gearBoxParams.ratios[2] = 3.6f;
    e.gearBoxParams.ratios[3] = 2.1f;
    e.gearBoxParams.ratios[4] = 1.4f;
    e.gearBoxParams.ratios[5] = 1.0f;
    e.gearBoxParams.nbRatios = 6;
    e.gearBoxParams.finalRatio = 4.2f;
    e.gearBoxParams.switchTime = 0.4f;

    for (PxU32 i = 0; i < 4; ++i) {
        e.fourWheelDifferentialParams.torqueRatios[i] = 0.25f;
        e.fourWheelDifferentialParams.aveWheelSpeedRatios[i] = 0.25f;
        e.multiWheelDifferentialParams.torqueRatios[i] = 0.25f;
        e.multiWheelDifferentialParams.aveWheelSpeedRatios[i] = 0.25f;
    }
    e.fourWheelDifferentialParams.frontWheelIds[0] = FL;
    e.fourWheelDifferentialParams.frontWheelIds[1] = FR;
    e.fourWheelDifferentialParams.rearWheelIds[0] = RL;
    e.fourWheelDifferentialParams.rearWheelIds[1] = RR;
    e.fourWheelDifferentialParams.centerBias = 1.3f;
    e.fourWheelDifferentialParams.centerTarget = 1.29f;
    e.fourWheelDifferentialParams.frontBias = 1.3f;
    e.fourWheelDifferentialParams.frontTarget = 1.29f;
    e.fourWheelDifferentialParams.rearBias = 1.3f;
    e.fourWheelDifferentialParams.rearTarget = 1.29f;
    e.fourWheelDifferentialParams.rate = 10.0f;

    e.clutchParams.accuracyMode = PxVehicleClutchAccuracyMode::eBEST_POSSIBLE;
    e.clutchParams.estimateIterations = 5;
}

static bool initPhysics() {
    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale());
    if (!gPhysics) return false;

    PxSceneDesc sd(gPhysics->getTolerancesScale());
    sd.gravity = gGravity;
    gDispatcher = PxDefaultCpuDispatcherCreate(0);
    sd.cpuDispatcher = gDispatcher;
    sd.filterShader = PxDefaultSimulationFilterShader;
    gScene = gPhysics->createScene(sd);
    gMaterial = gPhysics->createMaterial(0.8f, 0.8f, 0.3f);
    if (!PxInitVehicleExtension(*gFoundation)) return false;

    const PxCookingParams cooking(gPhysics->getTolerancesScale());
    if (!initTerrain(cooking)) return false;

    setBaseParams(gVehicle.mBaseParams);
    setEngineParams(gVehicle.mEngineDriveParams);

    static PxVehiclePhysXMaterialFriction frictions[1];
    frictions[0].material = gMaterial;
    frictions[0].friction = 1.0f;
    const PxQueryFilterData qfd(PxFilterData(0, 0, 0, 0), PxQueryFlag::eSTATIC);
    gVehicle.mPhysXParams.create(
        gVehicle.mBaseParams.axleDescription, qfd, nullptr, frictions, 1, 1.0f,
        PxTransform(PxVec3(0.0f, 0.0f, 0.0f), PxQuat(PxIdentity)),
        PxVec3(0.90f, 0.40f, 1.90f),
        PxTransform(PxVec3(0.0f, 0.10f, 0.0f), PxQuat(PxIdentity)));

    if (!gVehicle.initialize(*gPhysics, cooking, *gMaterial,
                             EngineDriveVehicle::eDIFFTYPE_FOURWHEELDRIVE))
        return false;

    gVehicle.setUpActor(*gScene, PxTransform(PxVec3(0.0f, 1.02f, 0.0f), PxQuat(PxIdentity)), "jeep");
    const PxU32 first = gVehicle.mEngineDriveParams.gearBoxParams.neutralGear + 1;
    gVehicle.mEngineDriveState.gearboxState.currentGear = first;
    gVehicle.mEngineDriveState.gearboxState.targetGear = first;
    gVehicle.mTransmissionCommandState.targetGear =
        PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;

    gCtx.setToDefault();
    gCtx.frame = gVehicle.mBaseParams.frame;
    gCtx.scale = gVehicle.mBaseParams.scale;
    gCtx.gravity = gGravity;
    gCtx.physxScene = gScene;
    gCtx.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_ACCELERATION;
    return true;
}

// -------------------------------------------------------------- the driving

struct P2 {
    float x, z, r;
};
static const P2 gCourse[] = {{0.0f, 10.0f, 3.0f}, {4.0f, 24.0f, 3.0f}, {0.0f, 40.0f, 3.5f},
                             {-4.0f, 56.0f, 3.5f}, {0.0f, 70.0f, 4.0f}};
static const int NWP = 5;

// Pure pursuit needs a point on the path a fixed distance ahead. The path
// is the polyline from the spawn through the waypoints, so walk it.
static void lookahead(float px, float pz, int from, float dist, float& ox, float& oz) {
    float ax = 0.0f, az = 0.0f;
    if (from > 0) {
        ax = gCourse[from - 1].x;
        az = gCourse[from - 1].z;
    }
    float remaining = dist;
    for (int i = from; i < NWP; ++i) {
        const float bx = gCourse[i].x, bz = gCourse[i].z;
        const float sx = bx - ax, sz = bz - az;
        const float len = std::sqrt(sx * sx + sz * sz);
        if (len < 1e-4f) {
            ax = bx;
            az = bz;
            continue;
        }
        // How far along this segment the vehicle already is.
        float t = ((px - ax) * sx + (pz - az) * sz) / (len * len);
        t = std::clamp(t, 0.0f, 1.0f);
        const float startx = ax + sx * t, startz = az + sz * t;
        const float left = len * (1.0f - t);
        if (remaining <= left) {
            ox = startx + sx / len * remaining;
            oz = startz + sz / len * remaining;
            return;
        }
        remaining -= left;
        ax = bx;
        az = bz;
    }
    ox = gCourse[NWP - 1].x;
    oz = gCourse[NWP - 1].z;
}

struct Row {
    float t, px, py, pz, qx, qy, qz, qw, speed;
    int grounded;
    float throttle, brake, steer;
};

int main() {
    if (!initPhysics()) {
        std::printf("initialisation failed\n");
        return 1;
    }

    const float dt = 1.0f / 60.0f;
    const float settleUntil = 2.0f;
    const float maxT = 90.0f;
    const float maxSteer = 35.0f * PxPi / 180.0f;
    const float wheelBase = 2.70f;

    std::vector<Row> rows;
    rows.reserve(6000);

    int wp = 0;
    float t = 0.0f;
    float speedIntegral = 0.0f;

    while (t <= maxT) {
        const PxTransform pose = gVehicle.mPhysXState.physxActor.rigidBody->getGlobalPose();
        const PxVec3 vel = gVehicle.mBaseState.rigidBodyState.linearVelocity;
        const PxVec3 fwd = pose.q.rotate(PxVec3(0.0f, 0.0f, 1.0f));
        const float speed = vel.dot(fwd);

        // Arrival is decided here but acted on after the row is stored, so
        // the trace contains the tick the vehicle was inside the waypoint.
        bool done = false;
        if (t >= settleUntil) {
            const float dx = gCourse[wp].x - pose.p.x, dz = gCourse[wp].z - pose.p.z;
            if (std::sqrt(dx * dx + dz * dz) <= gCourse[wp].r) {
                ++wp;
                if (wp >= NWP) done = true;
            }
        }

        float throttle = 0.0f, brake = 0.0f, steer = 0.0f;
        if (t >= settleUntil) {
            // Slow into the corners and over the cross slope, which will
            // roll a vehicle taken quickly.
            const bool onSlope = pose.p.z > 44.0f && pose.p.z < 64.0f;
            const float target = onSlope ? 4.5f : 8.0f;

            float lx = 0.0f, lz = 0.0f;
            lookahead(pose.p.x, pose.p.z, std::min(wp, NWP - 1), onSlope ? 5.0f : 7.0f, lx, lz);

            // Pure pursuit: the steer angle that puts the vehicle on an arc
            // through the lookahead point.
            const float dx = lx - pose.p.x, dz = lz - pose.p.z;
            const float dist = std::max(0.5f, std::sqrt(dx * dx + dz * dz));
            const float heading = std::atan2(fwd.x, fwd.z);
            float alpha = std::atan2(dx, dz) - heading;
            while (alpha > PxPi) alpha -= 2.0f * PxPi;
            while (alpha < -PxPi) alpha += 2.0f * PxPi;
            const float delta = std::atan2(2.0f * wheelBase * std::sin(alpha), dist);
            steer = std::clamp(delta / maxSteer, -1.0f, 1.0f);

            // PI on speed error, with the integral bled off when saturated.
            const float err = target - speed;
            speedIntegral = std::clamp(speedIntegral + err * dt, -4.0f, 4.0f);
            const float demand = 0.28f * err + 0.06f * speedIntegral;
            if (demand >= 0.0f) {
                throttle = std::clamp(demand, 0.0f, 1.0f);
                if (throttle >= 1.0f) speedIntegral -= err * dt;
            } else {
                brake = std::clamp(-demand, 0.0f, 0.6f);
            }
            throttle *= std::max(0.4f, 1.0f - 0.8f * std::fabs(steer));
        }

        gVehicle.mCommandState.nbBrakes = 1;
        gVehicle.mCommandState.brakes[0] = brake;
        gVehicle.mCommandState.throttle = throttle;
        gVehicle.mCommandState.steer = steer;

        int grounded = 0;
        for (PxU32 i = 0; i < 4; ++i)
            if (gVehicle.mBaseState.roadGeomStates[i].hitState) ++grounded;

        rows.push_back({t, pose.p.x, pose.p.y, pose.p.z, pose.q.x, pose.q.y, pose.q.z,
                        pose.q.w, speed, grounded, throttle, brake, steer});
        if (done) break;

        gVehicle.step(dt, gCtx);
        gScene->simulate(dt);
        gScene->fetchResults(true);
        t += dt;
    }

    std::FILE* out = std::fopen("trace.csv", "w");
    if (!out) return 1;
    std::fprintf(out, "t,px,py,pz,qx,qy,qz,qw,speed,wheels_grounded,throttle,brake,steer\n");
    for (const Row& r : rows)
        std::fprintf(out, "%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.4f,%d,%.3f,%.3f,%.3f\n",
                     double(r.t), double(r.px), double(r.py), double(r.pz), double(r.qx),
                     double(r.qy), double(r.qz), double(r.qw), double(r.speed), r.grounded,
                     double(r.throttle), double(r.brake), double(r.steer));
    std::fclose(out);

    std::printf("finished at t=%.2f, waypoints reached %d of %d\n", double(t), wp, NWP);
    return 0;
}
