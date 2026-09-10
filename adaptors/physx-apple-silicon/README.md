# PhysX 5 on Apple Silicon

NVIDIA ships PhysX 5 build presets for Linux and Windows only. There is no
macOS platform directory in the CMake tree, which reads as though the SDK
does not support Apple at all. It mostly does. What is missing is build
wiring and a handful of places that still assume a Mac is an Intel Mac.

This adaptor adds the wiring and fixes those places. The result builds
clean, and the vehicle maths runs.

```
./build.sh                 # clone if needed, patch, configure, build, smoke test
python3 apply.py <physx>   # just the patch, against an existing checkout
```

Verified on macOS 26.6.2, Apple clang 21.0.0, CMake 4.1.2. Eight static
libraries, arm64, 428 objects, zero errors.

## What was already there

Worth stating, because it is the reason this is an adaptor and not a port:

- `PxPreprocessor.h` sets `PX_OSX` from `__APPLE__` and puts Apple inside
  `PX_UNIX_FAMILY`.
- `PxUnixFPU.h` guards on `PX_LINUX || PX_OSX`, not on Linux alone.
- `PX_NEON` is selected from `__ARM_NEON`, which Apple clang defines, and a
  complete NEON vecmath implementation sits in `include/foundation/unix/neon`.
- `GetCompilerAndPlatform.cmake` and `cmake_generate_projects.py` both still
  accept a `mac` target, and 22 files across the SDK reference `PX_APPLE`,
  `PX_OSX` or `__APPLE__`.

## What the adaptor adds

**Build wiring.** A `source/compiler/cmake/mac` platform directory derived
from the Linux one, a `mac-aarch64-clang` preset, a `macAarch64` target in
the project generator, and an arm64 branch in
`GetCompilerAndPlatform.cmake`, which hardcoded `mac.x86_${LIBPATH_SUFFIX}`.

In the derived platform directory: the libstdc++ ABI selector goes, `-m64`
goes because Apple clang rejects it on arm64, `-gdwarf-2` becomes `-g`, the
shared library extension becomes `.dylib`, and `-Werror` comes off the
`-Weverything` warning set, because Apple clang is newer than the compiler
that list was tuned against and any new diagnostic would otherwise fail the
build.

**Five source fixes.** Each one enables something the tree already contains.

1. `PxVecMath.h` gated `COMPILE_VECTOR_INTRINSICS` on Intel or Switch, so
   every ARM target fell back to the scalar vecmath, which does not compile:
   `PxSIMDHelpers.h` calls `V4StoreU` with a `Vec3V`, and the scalar build
   makes `Vec3V` and `Vec4V` distinct types. Adding a `PX_NEON` branch
   reaches the NEON implementation that `PxUnixAoS.h` already dispatches to.

2. `PxUnixNeonInlineAoS.h` has three splat templates that select a lane with
   a runtime `if`, but `vdupq_lane_f32` needs a compile time lane, so
   instantiating index 2 or 3 passes an out of range lane to `vget_low`. The
   `#else` branch of the same guard already holds correct explicit
   specialisations, used on Switch. Apple is now routed to them.

3. `PxBitUtils.h` overloads `PxHighestSetBit` on `uint32_t` and `uint64_t`.
   On Darwin `uint64_t` is `unsigned long long`, so `unsigned long` is a
   third distinct 64 bit type and `size_t` arithmetic makes the call
   ambiguous. On LP64 Linux `uint64_t` is `unsigned long` and the question
   never arises, which is why upstream has not needed the overload.

4. `FdUnixFPU.cpp` treats `PX_OSX` as implying SSE, which was true on Intel
   Macs. `PxFPUGuard` now uses FPCR on Apple Silicon, preserving the intent
   of the Intel path: save the control word, mask exceptions, and turn on
   flush to zero. Note that Apple Silicon does not trap floating point
   exceptions, so `PxEnableFPExceptions` sets the enable bits and the
   hardware ignores them. That is written down rather than hidden.

5. `SnSerialUtils.cpp` had no binary platform tag for macOS arm64. Added as
   `MA64`, mirroring `LA64` for Linux aarch64. Serialized binary data is
   platform specific, so this has to be a distinct tag rather than reusing
   `mac64`, which means Intel.

## Note on the NEON path

Points 1 and 2 together imply that the shipped `linux-aarch64` presets also
build scalar vecmath, and that the NEON splat bug has therefore never been
compiled by anyone building from these presets. If you care about ARM
performance on Linux as well as macOS, that is worth knowing.

## Smoke test

`smoke.cpp` creates a foundation and a physics instance, initialises the
vehicle extension, and solves sprung masses for an asymmetric four wheel
layout. Wheel positions are centre of mass relative, so with the front axle
1.0 m ahead and the rear 1.8 m behind, the front should carry 1.8/2.8 of the
total by lever arm. It does, exactly:

```
sprung masses: 482.143 482.143 267.857 267.857  (sum 1500.000, expected 1500.000)
front axle: 964.286 kg, lever arms predict 964.286
OK: PhysX 5 vehicle maths runs on Apple Silicon
```

That is arithmetic through the NEON vecmath path this adaptor turns on, so
it tests the fix rather than just the link.

## Licensing

The adaptor is MIT, like the rest of this repository. PhysX itself is
NVIDIA's, under its own BSD 3 clause licence. Nothing from the PhysX source
tree is copied here: `apply.py` patches a checkout you make yourself.
