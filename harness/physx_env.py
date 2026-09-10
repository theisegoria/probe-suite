"""Find a built PhysX 5 SDK and describe how to compile against it.

The jeep benchmark is the only part of the suite that needs a compiled
third party SDK, so the discovery lives here rather than being spread
through the runner. Nothing is hardcoded to one platform: the search looks
for the vehicle library by name under whatever bin tree the build produced.

Set PROBE_PHYSX_ROOT to the "physx" directory of a checkout to skip the
search, and PROBE_PHYSX_LIBDIR to point at a specific build.
"""

import os
import platform

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The snippet helpers NVIDIA ships with the SDK. The task tells the model
# these are available, so the harness has to compile them in.
SNIPPET_SOURCES = (
    "base/Base.cpp",
    "enginedrivetrain/EngineDrivetrain.cpp",
    "physxintegration/PhysXIntegration.cpp",
)

LINK_ORDER = (
    "PhysXExtensions", "PhysXVehicle", "PhysX", "PhysXPvdSDK",
    "PhysXCooking", "PhysXCommon", "PhysXFoundation",
)


class PhysXMissing(Exception):
    pass


def _looks_like_physx(path):
    return os.path.isdir(os.path.join(path, "include", "vehicle"))


def find_root():
    """PROBE_PHYSX_ROOT wins if it is set, and is an error if it is wrong.

    Falling back to the bundled checkout when someone has pointed the
    variable somewhere else would build against a different SDK than they
    asked for and say nothing about it, which is the worst way to be
    helpful."""
    env = os.environ.get("PROBE_PHYSX_ROOT")
    if env:
        if _looks_like_physx(env):
            return env
        raise PhysXMissing(
            "PROBE_PHYSX_ROOT is set to %s, which has no include/vehicle, so it "
            "is not the physx directory of a PhysX 5 checkout. Point it at the "
            "'physx' directory inside the checkout, or unset it to use "
            "engines/PhysX." % env)

    bundled = os.path.join(ROOT, "engines", "PhysX", "physx")
    if _looks_like_physx(bundled):
        return bundled
    raise PhysXMissing(
        "no PhysX 5 checkout found. Clone NVIDIA-Omniverse/PhysX into "
        "engines/PhysX, or set PROBE_PHYSX_ROOT to the physx directory of "
        "your own checkout. On Apple Silicon, adaptors/physx-apple-silicon/"
        "build.sh does the whole thing.")


def find_libdir(root):
    env = os.environ.get("PROBE_PHYSX_LIBDIR")
    if env:
        return env
    checkout = os.path.dirname(root)
    seen = []
    for base in (os.path.join(checkout, "out-mac-arm64", "bin"),
                 os.path.join(root, "bin")):
        if not os.path.isdir(base):
            continue
        for dirpath, _dirs, files in os.walk(base):
            for f in files:
                if f.startswith(("libPhysXVehicle", "PhysXVehicle")) and \
                        f.endswith((".a", ".lib")):
                    seen.append(dirpath)
    if not seen:
        raise PhysXMissing(
            "PhysX is checked out but not built: no vehicle library under "
            "%s. Build it first." % root)
    # Prefer a release build if the tree has several.
    for d in seen:
        if "release" in d.lower():
            return d
    return seen[0]


def describe():
    """Everything the compiler needs, or an exception saying what is missing."""
    root = find_root()
    libdir = find_libdir(root)
    snippets = os.path.join(root, "snippets", "snippetvehiclecommon")
    if not os.path.isdir(snippets):
        raise PhysXMissing("this checkout has no snippets/snippetvehiclecommon")
    sources = [os.path.join(snippets, s) for s in SNIPPET_SOURCES]
    missing = [s for s in sources if not os.path.exists(s)]
    if missing:
        raise PhysXMissing("snippet sources missing: %s" % ", ".join(missing))

    cxxflags = ["-DPX_PHYSX_STATIC_LIB",
                "-I" + os.path.join(root, "include"),
                "-I" + snippets,
                "-I" + os.path.join(root, "snippets")]
    if platform.system() == "Darwin" and platform.machine() == "arm64":
        cxxflags += ["-arch", "arm64"]

    ldflags = ["-L" + libdir]
    suffix = "_static_64"
    for name in LINK_ORDER:
        ldflags.append("-l" + name + suffix)
    if platform.system() == "Linux":
        ldflags += ["-lpthread", "-ldl"]

    return {"root": root, "libdir": libdir, "snippets": snippets,
            "sources": sources, "cxxflags": cxxflags, "ldflags": ldflags}


def available():
    try:
        d = describe()
        return True, d["libdir"]
    except PhysXMissing as e:
        return False, str(e).split(".")[0]
