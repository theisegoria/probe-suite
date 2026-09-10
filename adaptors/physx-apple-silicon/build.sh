#!/bin/bash
# Build PhysX 5 for Apple Silicon, end to end.
#
#   ./build.sh [path-to-checkout]
#
# Clones PhysX if the path does not exist, applies the adaptor, configures,
# builds, then compiles and runs a smoke test that exercises the vehicle
# maths. Every step is checked: this refuses to report success on a build
# that did not produce libraries or a smoke test that did not pass.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$HERE/../../engines/PhysX}"

if [ ! -d "$SRC" ]; then
  echo "== cloning PhysX into $SRC"
  git clone --depth 1 https://github.com/NVIDIA-Omniverse/PhysX.git "$SRC"
fi
PX="$SRC/physx"
[ -d "$PX" ] || { echo "not a PhysX checkout: $SRC"; exit 1; }

echo "== applying the Apple Silicon adaptor"
python3 "$HERE/apply.py" "$PX"

BUILD="$SRC/build-mac-arm64"
OUT="$SRC/out-mac-arm64"
echo "== configuring"
# CMAKE_POLICY_VERSION_MINIMUM is needed because CMake 4 dropped compatibility
# with the pre-3.5 minimums PhysX still declares.
cmake -S "$PX/compiler/public" -B "$BUILD" -G Ninja \
  -DPHYSX_ROOT_DIR="$PX" \
  -DTARGET_BUILD_PLATFORM=mac \
  -DPX_OUTPUT_ARCH=arm \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE=release \
  -DPX_GENERATE_STATIC_LIBRARIES=ON \
  -DPX_BUILDSNIPPETS=OFF \
  -DPX_BUILDPVDRUNTIME=OFF \
  -DPX_GENERATE_GPU_PROJECTS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DPX_OUTPUT_LIB_DIR="$OUT" \
  -DPX_OUTPUT_BIN_DIR="$OUT"

echo "== building"
cmake --build "$BUILD" -j "$(sysctl -n hw.ncpu)"

LIB="$OUT/bin/mac.arm64/release"
[ -f "$LIB/libPhysXVehicle_static_64.a" ] || { echo "FAIL: no vehicle library at $LIB"; exit 1; }
echo "== libraries"
ls -1 "$LIB"/*.a | sed 's|.*/|  |'
lipo -info "$LIB/libPhysXVehicle_static_64.a"

echo "== smoke test"
c++ -std=c++17 -arch arm64 -O2 -DPX_PHYSX_STATIC_LIB -I"$PX/include" \
  "$HERE/smoke.cpp" -o "$HERE/smoke" -L"$LIB" \
  -lPhysXExtensions_static_64 -lPhysXVehicle_static_64 -lPhysX_static_64 \
  -lPhysXPvdSDK_static_64 -lPhysXCooking_static_64 -lPhysXCommon_static_64 \
  -lPhysXFoundation_static_64
"$HERE/smoke"
