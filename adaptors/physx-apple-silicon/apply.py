#!/usr/bin/env python3
"""Add an Apple Silicon target to a PhysX 5 checkout.

Upstream ships build presets for Linux and Windows only, but the SDK itself
still carries Apple support: PxPreprocessor sets PX_OSX from __APPLE__ and
puts Apple inside PX_UNIX_FAMILY, the Unix FPU and intrinsics headers guard
on PX_LINUX || PX_OSX, PX_NEON is selected from __ARM_NEON rather than per
platform, and both GetCompilerAndPlatform.cmake and cmake_generate_projects
already accept a "mac" target. What is missing is build wiring.

This script adds it, by deriving a mac platform directory from the Linux one
and fixing the places that assume x86. It is idempotent and it prints every
change it makes.

  python3 apply.py /path/to/PhysX/physx
"""

import io
import os
import re
import shutil
import sys

# Each edit says why it exists, because a year from now the reason is the
# only thing that makes the diff reviewable.
EDITS = [
    ("-D_GLIBCXX_USE_CXX11_ABI=1 ", "",
     "libstdc++ ABI selector, meaningless against libc++"),
    ("-D_GLIBCXX_USE_CXX11_ABI=1", "",
     "libstdc++ ABI selector, meaningless against libc++"),
    ('SET(CLANG_WARNINGS "-ferror-limit=0 -Wall -Wextra -Werror -Weverything',
     'SET(CLANG_WARNINGS "-ferror-limit=0 -Wall -Wextra -Weverything',
     "drop -Werror: Apple clang is newer than the one this warning list was "
     "tuned against, and -Weverything plus -Werror turns any new diagnostic "
     "into a build failure"),
    ("-O0 -g3 -gdwarf-2", "-O0 -g",
     "dwarf-2 is not what the Apple linker and dsymutil expect"),
    ("-O3 -g3 -gdwarf-2", "-O3 -g",
     "dwarf-2 is not what the Apple linker and dsymutil expect"),
    ('SET(PXFOUNDATION_PLATFORM_LINK_FLAGS "-m64")',
     'SET(PXFOUNDATION_PLATFORM_LINK_FLAGS "")',
     "-m64 is an x86 flag and Apple clang rejects it on arm64"),
    ('_64.so', '_64.dylib',
     "shared library extension on Darwin"),
]


# Patches to the SDK source itself. Each one enables something the tree
# already contains but cannot reach on this platform.
SOURCE_PATCHES = [
    ("include/foundation/PxVecMath.h",
     "#if PX_INTEL_FAMILY && (!defined(__EMSCRIPTEN__) || defined(__SSE2__))\n"
     "\t#define COMPILE_VECTOR_INTRINSICS 1\n"
     "#elif PX_SWITCH",
     "#if PX_INTEL_FAMILY && (!defined(__EMSCRIPTEN__) || defined(__SSE2__))\n"
     "\t#define COMPILE_VECTOR_INTRINSICS 1\n"
     "#elif PX_NEON\n"
     "\t// The NEON implementation is present in unix/neon and PxUnixAoS.h already\n"
     "\t// dispatches to it on PX_NEON. Without this branch every ARM target falls\n"
     "\t// back to the scalar vecmath, which does not compile: PxSIMDHelpers.h calls\n"
     "\t// V4StoreU with a Vec3V, and the scalar build makes Vec3V and Vec4V\n"
     "\t// distinct types.\n"
     "\t#define COMPILE_VECTOR_INTRINSICS 1\n"
     "#elif PX_SWITCH",
     "reach the NEON vecmath that unix/neon already provides"),

    ("include/foundation/PxBitUtils.h",
     "PX_INLINE uint32_t PxHighestSetBit(uint64_t x)\n"
     "{\n"
     "\tPX_ASSERT(x);\n"
     "\treturn PxHighestSetBitUnsafe(x);\n"
     "}",
     "PX_INLINE uint32_t PxHighestSetBit(uint64_t x)\n"
     "{\n"
     "\tPX_ASSERT(x);\n"
     "\treturn PxHighestSetBitUnsafe(x);\n"
     "}\n"
     "\n"
     "#if PX_APPLE_FAMILY\n"
     "/*!\n"
     "On Darwin uint64_t is unsigned long long, so unsigned long is a third\n"
     "distinct 64 bit type and size_t arithmetic makes the two overloads above\n"
     "ambiguous. On LP64 Linux uint64_t is unsigned long and the question never\n"
     "arises, which is why upstream has not needed this.\n"
     "*/\n"
     "PX_INLINE uint32_t PxHighestSetBit(unsigned long x)\n"
     "{\n"
     "\tPX_ASSERT(x);\n"
     "\treturn PxHighestSetBitUnsafe(uint64_t(x));\n"
     "}\n"
     "#endif",
     "resolve the size_t overload ambiguity Darwin's type widths create"),

    ("source/foundation/unix/FdUnixFPU.cpp",
     "#if PX_OSX\n// osx defines SIMD as standard for floating point operations.\n#include <xmmintrin.h>\n#endif",
     "#if PX_OSX && (PX_X86 || PX_X64)\n// Intel macs define SIMD as standard for floating point operations.\n#include <xmmintrin.h>\n#endif",
     "the x86 intrinsics header was included for every Mac, Apple Silicon included"),

    ("source/foundation/unix/FdUnixFPU.cpp",
     "#elif PX_OSX\n\tmControlWords[0] = _mm_getcsr();",
     "#elif PX_OSX && PX_NEON\n"
     "\t// Apple Silicon. The MXCSR equivalent is FPCR. Preserve the semantics of\n"
     "\t// the Intel path: save the control word, mask all exceptions, and turn on\n"
     "\t// flush to zero (FPCR bit 24), which stands in for both FTZ and DAZ.\n"
     "\tuint64_t fpcr;\n"
     "\t__asm__ __volatile__(\"mrs %0, fpcr\" : \"=r\"(fpcr));\n"
     "\tmControlWords[0] = uint32_t(fpcr);\n"
     "\tfpcr &= ~((uint64_t(1) << 8) | (uint64_t(1) << 9) | (uint64_t(1) << 10) |\n"
     "\t          (uint64_t(1) << 11) | (uint64_t(1) << 12) | (uint64_t(1) << 15));\n"
     "\tfpcr |= (uint64_t(1) << 24);\n"
     "\t__asm__ __volatile__(\"msr fpcr, %0\" : : \"r\"(fpcr));\n"
     "#elif PX_OSX\n\tmControlWords[0] = _mm_getcsr();",
     "PxFPUGuard construction on Apple Silicon, through FPCR instead of MXCSR"),

    ("source/foundation/unix/FdUnixFPU.cpp",
     "#elif PX_OSX\n\t// restore control word and clear exception flags",
     "#elif PX_OSX && PX_NEON\n"
     "\tuint64_t fpcr;\n"
     "\t__asm__ __volatile__(\"mrs %0, fpcr\" : \"=r\"(fpcr));\n"
     "\tfpcr = (fpcr & ~uint64_t(0xffffffff)) | uint64_t(mControlWords[0]);\n"
     "\t__asm__ __volatile__(\"msr fpcr, %0\" : : \"r\"(fpcr));\n"
     "#elif PX_OSX\n\t// restore control word and clear exception flags",
     "PxFPUGuard destruction on Apple Silicon"),

    ("source/foundation/unix/FdUnixFPU.cpp",
     "#elif PX_OSX\n\t// clear any pending exceptions\n"
     "\t// (setting exception state flags cause exceptions on the first following fp operation)\n"
     "\tuint32_t control = _mm_getcsr() & ~_MM_EXCEPT_MASK;\n"
     "\n"
     "\t// enable all fp exceptions except inexact and underflow (common, benign)",
     "#elif PX_OSX && PX_NEON\n"
     "\t// Set the FPCR enable bits for invalid, divide by zero and overflow, to\n"
     "\t// match the Linux path. Note that Apple Silicon does not actually trap\n"
     "\t// floating point exceptions: these bits are accepted and ignored, so this\n"
     "\t// is written for parity of intent rather than of effect.\n"
     "\tuint64_t fpcr;\n"
     "\t__asm__ __volatile__(\"mrs %0, fpcr\" : \"=r\"(fpcr));\n"
     "\tfpcr |= (uint64_t(1) << 8) | (uint64_t(1) << 9) | (uint64_t(1) << 10);\n"
     "\t__asm__ __volatile__(\"msr fpcr, %0\" : : \"r\"(fpcr));\n"
     "#elif PX_OSX\n\t// clear any pending exceptions\n"
     "\t// (setting exception state flags cause exceptions on the first following fp operation)\n"
     "\tuint32_t control = _mm_getcsr() & ~_MM_EXCEPT_MASK;\n"
     "\n"
     "\t// enable all fp exceptions except inexact and underflow (common, benign)",
     "PxEnableFPExceptions on Apple Silicon"),

    ("source/foundation/unix/FdUnixFPU.cpp",
     "#elif PX_OSX\n\t// clear any pending exceptions\n"
     "\t// (setting exception state flags cause exceptions on the first following fp operation)\n"
     "\tuint32_t control = _mm_getcsr() & ~_MM_EXCEPT_MASK;\n"
     "\t_mm_setcsr(control | _MM_MASK_MASK);",
     "#elif PX_OSX && PX_NEON\n"
     "\tuint64_t fpcr;\n"
     "\t__asm__ __volatile__(\"mrs %0, fpcr\" : \"=r\"(fpcr));\n"
     "\tfpcr &= ~((uint64_t(1) << 8) | (uint64_t(1) << 9) | (uint64_t(1) << 10));\n"
     "\t__asm__ __volatile__(\"msr fpcr, %0\" : : \"r\"(fpcr));\n"
     "#elif PX_OSX\n\t// clear any pending exceptions\n"
     "\t// (setting exception state flags cause exceptions on the first following fp operation)\n"
     "\tuint32_t control = _mm_getcsr() & ~_MM_EXCEPT_MASK;\n"
     "\t_mm_setcsr(control | _MM_MASK_MASK);",
     "PxDisableFPExceptions on Apple Silicon"),
    ("include/foundation/unix/neon/PxUnixNeonInlineAoS.h",
     "#if !PX_SWITCH\ntemplate <int index>\nPX_FORCE_INLINE BoolV BSplatElement(BoolV a)",
     "#if !PX_SWITCH && !PX_APPLE_FAMILY\ntemplate <int index>\nPX_FORCE_INLINE BoolV BSplatElement(BoolV a)",
     "BSplatElement: route Apple to the explicit lane specialisations. The generic template selects the lane with a runtime if, but vdupq_lane_f32 needs a compile time lane, so instantiating index 2 or 3 passes an out of range lane to vget_low. The #else branch already has correct specialisations and is used on Switch"),
    ("include/foundation/unix/neon/PxUnixNeonInlineAoS.h",
     "#if !PX_SWITCH\ntemplate <int index>\nPX_FORCE_INLINE VecU32V V4U32SplatElement(VecU32V a)",
     "#if !PX_SWITCH && !PX_APPLE_FAMILY\ntemplate <int index>\nPX_FORCE_INLINE VecU32V V4U32SplatElement(VecU32V a)",
     "V4U32SplatElement: route Apple to the explicit lane specialisations. The generic template selects the lane with a runtime if, but vdupq_lane_f32 needs a compile time lane, so instantiating index 2 or 3 passes an out of range lane to vget_low. The #else branch already has correct specialisations and is used on Switch"),
    ("include/foundation/unix/neon/PxUnixNeonInlineAoS.h",
     "#if !PX_SWITCH\ntemplate <int index>\nPX_FORCE_INLINE Vec4V V4SplatElement(Vec4V a)",
     "#if !PX_SWITCH && !PX_APPLE_FAMILY\ntemplate <int index>\nPX_FORCE_INLINE Vec4V V4SplatElement(Vec4V a)",
     "V4SplatElement: route Apple to the explicit lane specialisations. The generic template selects the lane with a runtime if, but vdupq_lane_f32 needs a compile time lane, so instantiating index 2 or 3 passes an out of range lane to vget_low. The #else branch already has correct specialisations and is used on Switch"),
    ("source/physxextensions/src/serialization/SnSerialUtils.cpp",
     "#define SN_NUM_BINARY_PLATFORMS 9",
     "#define SN_NUM_BINARY_PLATFORMS 10",
     "make room for a macOS arm64 entry in the binary serialisation platform table"),

    ("source/physxextensions/src/serialization/SnSerialUtils.cpp",
     "\tPX_MAKE_FOURCC('L','A','6','4')\n};",
     "\tPX_MAKE_FOURCC('L','A','6','4'),\n\tPX_MAKE_FOURCC('M','A','6','4')\n};",
     "binary platform tag for macOS arm64, mirroring LA64 for Linux aarch64"),

    ("source/physxextensions/src/serialization/SnSerialUtils.cpp",
     "\t\"linuxaarch64\"\n};",
     "\t\"linuxaarch64\",\n\t\"macaarch64\"\n};",
     "binary platform name for macOS arm64"),

    ("source/physxextensions/src/serialization/SnSerialUtils.cpp",
     "#elif PX_LINUX && PX_A64\n\treturn sBinaryPlatformTags[8];\n#else",
     "#elif PX_LINUX && PX_A64\n\treturn sBinaryPlatformTags[8];\n"
     "#elif PX_OSX && PX_A64\n\treturn sBinaryPlatformTags[9];\n#else",
     "select the macOS arm64 tag. Serialized binary data is platform specific, "
     "so this must be a distinct tag rather than reusing mac64, which is x86"),

]


def patch_sources(root):
    for rel, old, new, why in SOURCE_PATCHES:
        p = os.path.join(root, rel)
        s = io.open(p, encoding="utf-8").read()
        if new in s:
            print("  %s already patched" % rel)
            continue
        if old not in s:
            print("  %s PATTERN NOT FOUND, check upstream changes" % rel)
            continue
        io.open(p, "w", encoding="utf-8").write(s.replace(old, new, 1))
        print("  %s: %s" % (rel, why))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = os.path.abspath(sys.argv[1])
    linux = os.path.join(root, "source", "compiler", "cmake", "linux")
    mac = os.path.join(root, "source", "compiler", "cmake", "mac")
    if not os.path.isdir(linux):
        sys.exit("not a PhysX physx/ directory: %s" % root)

    # 1. the platform directory
    if os.path.isdir(mac):
        shutil.rmtree(mac)
    shutil.copytree(linux, mac)
    changed = 0
    for name in sorted(os.listdir(mac)):
        p = os.path.join(mac, name)
        if not os.path.isfile(p):
            continue
        s = before = io.open(p, encoding="utf-8").read()
        # The per-module cmake files reference their own platform's variable
        # names, so the whole set is renamed together and stays self-contained.
        s = s.replace("PHYSX_LINUX_", "PHYSX_MAC_")
        for old, new, _why in EDITS:
            s = s.replace(old, new)
        if s != before:
            io.open(p, "w", encoding="utf-8").write(s)
            changed += 1
    print("created source/compiler/cmake/mac from linux (%d of %d files edited)"
          % (changed, len(os.listdir(mac))))

    # 2. the arch string, which is hardcoded to x86
    gcp = os.path.join(root, "source", "compiler", "cmake", "modules",
                       "GetCompilerAndPlatform.cmake")
    s = io.open(gcp, encoding="utf-8").read()
    old = 'ELSEIF(TARGET_BUILD_PLATFORM STREQUAL "mac")\n\t\tSET(RETVAL "mac.x86_${LIBPATH_SUFFIX}")'
    new = ('ELSEIF(TARGET_BUILD_PLATFORM STREQUAL "mac")\n'
           '\t\tIF(PX_OUTPUT_ARCH STREQUAL "arm" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")\n'
           '\t\t\tSET(RETVAL "mac.arm64")\n'
           '\t\tELSE()\n'
           '\t\t\tSET(RETVAL "mac.x86_${LIBPATH_SUFFIX}")\n'
           '\t\tENDIF()')
    if old in s:
        io.open(gcp, "w", encoding="utf-8").write(s.replace(old, new, 1))
        print("GetCompilerAndPlatform.cmake: mac target now resolves arm64 as well as x86")
    elif "mac.arm64" in s:
        print("GetCompilerAndPlatform.cmake: already patched")
    else:
        print("GetCompilerAndPlatform.cmake: PATTERN NOT FOUND, check upstream changes")

    # 3. a preset, so the normal entry point works too
    preset = os.path.join(root, "buildtools", "presets", "public",
                          "mac-aarch64-clang.xml")
    io.open(preset, "w", encoding="utf-8").write('''<?xml version="1.0" encoding="utf-8"?>
<preset name="mac-aarch64-clang" comment="macOS arm64 clang PhysX SDK settings">
  <platform targetPlatform="macAarch64" compiler="clang" />
  <CMakeSwitches>
    <cmakeSwitch name="PX_BUILDSNIPPETS" value="False" comment="Generate the snippets" />
    <cmakeSwitch name="PX_BUILDPVDRUNTIME" value="False" comment="OmniPVD is not built on this platform" />
    <cmakeSwitch name="PX_GENERATE_STATIC_LIBRARIES" value="True" comment="Generate static libs" />
    <cmakeSwitch name="PX_GENERATE_GPU_PROJECTS" value="False" comment="No CUDA on Apple silicon" />
    <cmakeSwitch name="PX_GENERATE_GPU_PROJECTS_ONLY" value="False" comment="" />
    <cmakeSwitch name="PX_SCALAR_MATH" value="False" comment="Keep NEON SIMD math" />
    <cmakeSwitch name="PX_FLOAT_POINT_PRECISE_MATH" value="False" comment="Float point precise math" />
  </CMakeSwitches>
  <CMakeParams>
    <cmakeParam name="CMAKE_INSTALL_PREFIX" value="install/mac-aarch64-clang/PhysX" comment="Install path relative to PhysX SDK root" />
  </CMakeParams>
</preset>
''')
    print("wrote buildtools/presets/public/mac-aarch64-clang.xml")

    # 4. teach the generator about the new target platform
    gen = os.path.join(root, "buildtools", "cmake_generate_projects.py")
    s = io.open(gen, encoding="utf-8").read()
    subs = [
        ("        if self.targetPlatform == 'linux':\n            return False\n"
         "        elif self.targetPlatform == 'linuxAarch64':\n            return False",
         "        if self.targetPlatform == 'linux':\n            return False\n"
         "        elif self.targetPlatform == 'linuxAarch64':\n            return False\n"
         "        elif self.targetPlatform == 'macAarch64':\n            return False"),
        ("        elif self.targetPlatform in ['linux', 'linuxAarch64']:",
         "        elif self.targetPlatform in ['linux', 'linuxAarch64', 'macAarch64']:"),
        ("        elif self.targetPlatform == 'mac64':",
         "        elif self.targetPlatform == 'macAarch64':\n"
         "            outString = outString + ' -DTARGET_BUILD_PLATFORM=mac'\n"
         "            outString = outString + ' -DPX_OUTPUT_ARCH=arm'\n"
         "            outString = outString + ' -DCMAKE_OSX_ARCHITECTURES=arm64'\n"
         "            outString = outString + ' -DCMAKE_C_COMPILER=clang'\n"
         "            outString = outString + ' -DCMAKE_CXX_COMPILER=clang++'\n"
         "            return outString\n"
         "        elif self.targetPlatform == 'mac64':"),
    ]
    done = 0
    for old, new in subs:
        if new.split("\n")[0] in s and "macAarch64" in s and old not in s:
            continue
        if old in s:
            s = s.replace(old, new, 1)
            done += 1
    if done:
        io.open(gen, "w", encoding="utf-8").write(s)
    print("cmake_generate_projects.py: %d edit(s) for the macAarch64 target" % done)

    # 5. the SDK source itself
    print("source patches:")
    patch_sources(root)

    print("\nEdits applied to the derived platform directory, and why:")
    for _old, _new, why in EDITS:
        print("  - %s" % why)


if __name__ == "__main__":
    main()
