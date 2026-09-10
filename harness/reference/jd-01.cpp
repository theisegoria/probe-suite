// Reference solution for jd-01. Its job is to prove the task is satisfiable.
// Parameters are built in code rather than read from NVIDIA's JSON, because
// that is what the task asks of a model.
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
static PxRigidStatic* gTerrain = nullptr;
static EngineDriveVehicle gVehicle;
static PxVehiclePhysXSimulationContext gCtx;
static const PxVec3 gGravity(0.0f, -9.81f, 0.0f);

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
                int i = std::stoi(tok);
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
    if (!loadObj("assets/terrain.obj", verts, idx)) {
        std::printf("could not read assets/terrain.obj\n");
        return false;
    }
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

    gTerrain = gPhysics->createRigidStatic(PxTransform(PxIdentity));
    PxShape* shape = PxRigidActorExt::createExclusiveShape(
        *gTerrain, PxTriangleMeshGeometry(mesh), *gMaterial);
    shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    gScene->addActor(*gTerrain);
    mesh->release();
    return true;
}

// -------------------------------------------------------------- parameters

static void setBaseParams(BaseVehicleParams& p) {
    p.axleDescription.setToDefault();
    p.axleDescription.addAxle(2, (const PxU32[]){0, 1});   // front
    p.axleDescription.addAxle(2, (const PxU32[]){2, 3});   // rear

    p.frame.lngAxis = PxVehicleAxes::ePosZ;
    p.frame.latAxis = PxVehicleAxes::ePosX;
    p.frame.vrtAxis = PxVehicleAxes::ePosY;
    p.scale.scale = 1.0f;

    p.suspensionStateCalculationParams.suspensionJounceCalculationType =
        PxVehicleSuspensionJounceCalculationType::eSWEEP;
    p.suspensionStateCalculationParams.limitSuspensionExpansionVelocity = false;

    p.brakeResponseParams[0].maxResponse = 4000.0f;
    for (PxU32 i = 0; i < 4; ++i) p.brakeResponseParams[0].wheelResponseMultipliers[i] = 1.0f;
    p.brakeResponseParams[1].maxResponse = 0.0f;
    for (PxU32 i = 0; i < 4; ++i) p.brakeResponseParams[1].wheelResponseMultipliers[i] = 0.0f;

    p.steerResponseParams.maxResponse = 35.0f * PxPi / 180.0f;
    p.steerResponseParams.wheelResponseMultipliers[0] = 1.0f;   // front steers
    p.steerResponseParams.wheelResponseMultipliers[1] = 1.0f;
    p.steerResponseParams.wheelResponseMultipliers[2] = 0.0f;
    p.steerResponseParams.wheelResponseMultipliers[3] = 0.0f;

    p.ackermannParams[0].wheelIds[0] = 0;
    p.ackermannParams[0].wheelIds[1] = 1;
    p.ackermannParams[0].wheelBase = 2.70f;
    p.ackermannParams[0].trackWidth = 1.72f;
    p.ackermannParams[0].strength = 1.0f;

    // The actor origin is the centre of mass. A wheel centre sits at
    // attachment + travelDir * (travelDist - jounce), so for the wheel to
    // rest at y = -0.52 with the suspension a third compressed, the strut
    // mounts 0.25 * 2/3 above that.
    const PxVec3 attach[4] = {
        PxVec3(-0.86f, -0.353f,  1.35f), PxVec3(0.86f, -0.353f,  1.35f),
        PxVec3(-0.86f, -0.353f, -1.35f), PxVec3(0.86f, -0.353f, -1.35f)};

    const float sprung = 1500.0f / 4.0f;
    const float travel = 0.25f;
    const float stiffness = sprung * 9.81f / (travel / 3.0f);
    const float damping = 2.0f * std::sqrt(stiffness * sprung);

    for (PxU32 i = 0; i < 4; ++i) {
        p.suspensionParams[i].suspensionAttachment = PxTransform(attach[i], PxQuat(PxIdentity));
        p.suspensionParams[i].suspensionTravelDir = PxVec3(0.0f, -1.0f, 0.0f);
        p.suspensionParams[i].suspensionTravelDist = travel;
        p.suspensionParams[i].wheelAttachment = PxTransform(PxIdentity);

        p.suspensionComplianceParams[i].wheelToeAngle.addPair(0.0f, 0.0f);
        p.suspensionComplianceParams[i].wheelCamberAngle.addPair(0.0f, 0.0f);
        p.suspensionComplianceParams[i].suspForceAppPoint.addPair(0.0f, PxVec3(0.0f, -0.11f, 0.0f));
        p.suspensionComplianceParams[i].tireForceAppPoint.addPair(0.0f, PxVec3(0.0f, -0.11f, 0.0f));

        p.suspensionForceParams[i].stiffness = stiffness;
        p.suspensionForceParams[i].damping = damping;
        p.suspensionForceParams[i].sprungMass = sprung;

        p.tireForceParams[i].longStiff = 20000.0f;
        p.tireForceParams[i].latStiffX = 0.01f;
        p.tireForceParams[i].latStiffY = 80000.0f;
        p.tireForceParams[i].camberStiff = 0.0f;
        p.tireForceParams[i].restLoad = sprung * 9.81f;
        p.tireForceParams[i].frictionVsSlip[0][0] = 0.0f; p.tireForceParams[i].frictionVsSlip[0][1] = 1.0f;
        p.tireForceParams[i].frictionVsSlip[1][0] = 0.1f; p.tireForceParams[i].frictionVsSlip[1][1] = 1.0f;
        p.tireForceParams[i].frictionVsSlip[2][0] = 1.0f; p.tireForceParams[i].frictionVsSlip[2][1] = 1.0f;
        p.tireForceParams[i].loadFilter[0][0] = 0.0f; p.tireForceParams[i].loadFilter[0][1] = 0.2308f;
        p.tireForceParams[i].loadFilter[1][0] = 3.0f; p.tireForceParams[i].loadFilter[1][1] = 3.0f;

        p.wheelParams[i].radius = 0.36f;
        p.wheelParams[i].halfWidth = 0.15f;
        p.wheelParams[i].mass = 20.0f;
        p.wheelParams[i].moi = 0.5f * 20.0f * 0.36f * 0.36f;
        p.wheelParams[i].dampingRate = 0.25f;
    }

    p.rigidBodyParams.mass = 1500.0f;
    p.rigidBodyParams.moi = PxVec3(1885.0f, 2210.0f, 485.0f);
}

static void setEngineParams(EngineDrivetrainParams& e) {
    e.autoboxParams.latency = 2.0f;
    for (PxU32 i = 0; i < PxVehicleGearboxParams::eMAX_NB_GEARS; ++i) {
        e.autoboxParams.upRatios[i] = 0.65f;
        e.autoboxParams.downRatios[i] = 0.50f;
    }
    e.autoboxParams.upRatios[1] = 0.15f;  // neutral

    e.clutchCommandResponseParams.maxResponse = 10.0f;

    e.engineParams.torqueCurve.addPair(0.0f, 1.0f);
    e.engineParams.torqueCurve.addPair(0.33f, 1.0f);
    e.engineParams.torqueCurve.addPair(1.0f, 1.0f);
    e.engineParams.moi = 1.0f;
    e.engineParams.peakTorque = 600.0f;
    e.engineParams.idleOmega = 0.0f;
    e.engineParams.maxOmega = 600.0f;
    e.engineParams.dampingRateFullThrottle = 0.15f;
    e.engineParams.dampingRateZeroThrottleClutchEngaged = 2.0f;
    e.engineParams.dampingRateZeroThrottleClutchDisengaged = 0.35f;

    e.gearBoxParams.neutralGear = 1;
    e.gearBoxParams.ratios[0] = -4.0f;
    e.gearBoxParams.ratios[1] = 0.0f;
    e.gearBoxParams.ratios[2] = 4.0f;
    e.gearBoxParams.ratios[3] = 2.0f;
    e.gearBoxParams.ratios[4] = 1.5f;
    e.gearBoxParams.ratios[5] = 1.1f;
    e.gearBoxParams.nbRatios = 6;
    e.gearBoxParams.finalRatio = 4.0f;
    e.gearBoxParams.switchTime = 0.5f;

    for (PxU32 i = 0; i < 4; ++i) {
        e.fourWheelDifferentialParams.torqueRatios[i] = 0.25f;
        e.fourWheelDifferentialParams.aveWheelSpeedRatios[i] = 0.25f;
        e.multiWheelDifferentialParams.torqueRatios[i] = 0.25f;
        e.multiWheelDifferentialParams.aveWheelSpeedRatios[i] = 0.25f;
    }
    e.fourWheelDifferentialParams.frontWheelIds[0] = 0;
    e.fourWheelDifferentialParams.frontWheelIds[1] = 1;
    e.fourWheelDifferentialParams.rearWheelIds[0] = 2;
    e.fourWheelDifferentialParams.rearWheelIds[1] = 3;
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

// ------------------------------------------------------------------- setup

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

    // Not snippetvehicle::setPhysXIntegrationParams, which hardcodes the
    // chassis of NVIDIA's own demo car. This is the jeep the task describes:
    // origin at the centre of mass, a 1.80 by 0.80 by 3.80 box whose centre
    // is 0.10 m above it.
    static PxVehiclePhysXMaterialFriction frictions[1];
    frictions[0].material = gMaterial;
    frictions[0].friction = 1.0f;
    const PxQueryFilterData queryFilterData(PxFilterData(0, 0, 0, 0), PxQueryFlag::eSTATIC);
    gVehicle.mPhysXParams.create(
        gVehicle.mBaseParams.axleDescription,
        queryFilterData, nullptr,
        frictions, 1, 1.0f,
        PxTransform(PxVec3(0.0f, 0.0f, 0.0f), PxQuat(PxIdentity)),
        PxVec3(0.90f, 0.40f, 1.90f),
        PxTransform(PxVec3(0.0f, 0.10f, 0.0f), PxQuat(PxIdentity)));

    if (!gVehicle.initialize(*gPhysics, cooking, *gMaterial,
                             EngineDriveVehicle::eDIFFTYPE_FOURWHEELDRIVE))
        return false;

    gVehicle.setUpActor(*gScene, PxTransform(PxVec3(0.0f, 0.95f, 0.0f), PxQuat(PxIdentity)), "jeep");
    gVehicle.mEngineDriveState.gearboxState.currentGear = gVehicle.mEngineDriveParams.gearBoxParams.neutralGear + 1;
    gVehicle.mEngineDriveState.gearboxState.targetGear = gVehicle.mEngineDriveParams.gearBoxParams.neutralGear + 1;
    gVehicle.mTransmissionCommandState.targetGear = PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;

    gCtx.setToDefault();
    gCtx.frame = gVehicle.mBaseParams.frame;
    gCtx.scale = gVehicle.mBaseParams.scale;
    gCtx.gravity = gGravity;
    gCtx.physxScene = gScene;
    gCtx.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_ACCELERATION;
    return true;
}

// -------------------------------------------------------------- the driving

struct Waypoint { float x, z, r; };
static const Waypoint gCourse[] = {
    {0.0f, 10.0f, 3.0f}, {4.0f, 24.0f, 3.0f}, {0.0f, 40.0f, 3.5f},
    {-4.0f, 56.0f, 3.5f}, {0.0f, 70.0f, 4.0f}};
static const int gNbWaypoints = 5;

int main() {
    if (!initPhysics()) {
        std::printf("initialisation failed\n");
        return 1;
    }
    std::FILE* out = std::fopen("trace.csv", "w");
    if (!out) return 1;
    std::fprintf(out, "t,px,py,pz,qx,qy,qz,qw,speed,wheels_grounded,throttle,brake,steer\n");

    const float dt = 1.0f / 60.0f;
    const float settleUntil = 2.0f;
    const float maxT = 90.0f;
    int wp = 0;
    float t = 0.0f;

    while (t <= maxT) {
        const PxTransform pose = gVehicle.mPhysXState.physxActor.rigidBody->getGlobalPose();
        const PxVec3 vel = gVehicle.mBaseState.rigidBodyState.linearVelocity;
        const PxVec3 fwd = pose.q.rotate(PxVec3(0, 0, 1));
        const float speed = vel.dot(fwd);

        // Arrival is decided on this tick's pose but acted on after the row
        // has been written, so the trace actually contains the tick at which
        // the vehicle was inside the last waypoint. A reader that only sees
        // the file has to be able to see the finish.
        bool done = false;
        if (t >= settleUntil) {
            const Waypoint& a = gCourse[wp];
            const float ax = a.x - pose.p.x, az = a.z - pose.p.z;
            if (std::sqrt(ax * ax + az * az) <= a.r) {
                ++wp;
                if (wp >= gNbWaypoints) done = true;
            }
        }

        float throttle = 0.0f, brake = 0.0f, steer = 0.0f;
        if (t >= settleUntil) {
            const Waypoint& w = gCourse[done ? gNbWaypoints - 1 : wp];
            const float dx = w.x - pose.p.x, dz = w.z - pose.p.z;
            const float want = std::atan2(dx, dz);
            const float have = std::atan2(fwd.x, fwd.z);
            float err = want - have;
            while (err > PxPi) err -= 2.0f * PxPi;
            while (err < -PxPi) err += 2.0f * PxPi;
            steer = std::clamp(err / (35.0f * PxPi / 180.0f), -1.0f, 1.0f);

            // Slow for the cross slope, which will roll a vehicle taken fast.
            const float target = (pose.p.z > 44.0f && pose.p.z < 64.0f) ? 5.0f : 9.0f;
            if (speed < target) throttle = std::clamp(0.35f + (target - speed) * 0.20f, 0.0f, 1.0f);
            else brake = std::clamp((speed - target) * 0.25f, 0.0f, 0.6f);
            throttle *= std::max(0.35f, 1.0f - std::fabs(steer));
        }

        gVehicle.mCommandState.nbBrakes = 1;
        gVehicle.mCommandState.brakes[0] = brake;
        gVehicle.mCommandState.throttle = throttle;
        gVehicle.mCommandState.steer = steer;

        int grounded = 0;
        for (PxU32 i = 0; i < 4; ++i)
            if (gVehicle.mBaseState.roadGeomStates[i].hitState) ++grounded;

        std::fprintf(out, "%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.4f,%d,%.3f,%.3f,%.3f\n",
                     double(t), double(pose.p.x), double(pose.p.y), double(pose.p.z),
                     double(pose.q.x), double(pose.q.y), double(pose.q.z), double(pose.q.w),
                     double(speed), grounded, double(throttle), double(brake), double(steer));

        if (done) break;

        gVehicle.step(dt, gCtx);
        gScene->simulate(dt);
        gScene->fetchResults(true);
        t += dt;
    }
    std::fclose(out);
    std::printf("finished at t=%.2f, waypoints reached %d of %d\n", double(t), wp, gNbWaypoints);
    return 0;
}
