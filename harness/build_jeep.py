"""Compile and run one jeep submission against a built PhysX 5.

The snippet helper objects are compiled once and cached, because they do not
change between submissions and they are the slow part of the link.
"""

import os
import shutil
import subprocess
import tempfile

from . import physx_env

ROOT = physx_env.ROOT
CACHE = os.path.join(ROOT, "engines", ".probe-cache")
DEFAULT_CXX = os.environ.get("PROBE_CXX", "c++")
COMPILE_TIMEOUT = 300
RUN_TIMEOUT = 300
STD = os.environ.get("PROBE_JEEP_STD", "c++20")


def _snippet_objects(env, cxx):
    """Compile NVIDIA's snippet helpers once. Returns a list of object files."""
    os.makedirs(CACHE, exist_ok=True)
    objs = []
    for src in env["sources"]:
        obj = os.path.join(CACHE, os.path.basename(src)[:-4] + ".o")
        if not os.path.exists(obj) or os.path.getmtime(obj) < os.path.getmtime(src):
            cmd = [cxx, "-std=" + STD, "-O2", "-c", src, "-o", obj] + env["cxxflags"]
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
            if p.returncode != 0:
                raise physx_env.PhysXMissing(
                    "could not compile the PhysX snippet helper %s:\n%s"
                    % (os.path.basename(src), p.stderr[-2000:]))
        objs.append(obj)
    return objs


def prepare(workdir):
    """Give the submission the assets it is told it has."""
    dst = os.path.join(workdir, "assets")
    os.makedirs(dst, exist_ok=True)
    for name in ("terrain.obj", "jeep.obj"):
        shutil.copyfile(os.path.join(ROOT, "assets", name), os.path.join(dst, name))


def compile_cpp(source_text, workdir, cxx=None):
    cxx = cxx or DEFAULT_CXX
    env = physx_env.describe()
    objs = _snippet_objects(env, cxx)
    src = os.path.join(workdir, "main.cpp")
    with open(src, "w", encoding="utf-8") as fh:
        fh.write(source_text)
    exe = os.path.join(workdir, "jeep")
    cmd = ([cxx, "-std=" + STD, "-O2", src] + objs + ["-o", exe]
           + env["cxxflags"] + env["ldflags"])
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"ok": False, "stderr": "compiler timed out after %ds" % COMPILE_TIMEOUT,
                "cmd": " ".join(cmd)}
    return {"ok": p.returncode == 0 and os.path.exists(exe),
            "stderr": (p.stderr or "") + (p.stdout or ""),
            "cmd": " ".join(cmd), "exe": exe}


def run_exe(exe, workdir):
    try:
        p = subprocess.run([exe], cwd=workdir, capture_output=True, text=True,
                           timeout=RUN_TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"ok": False, "why": "the program ran for more than %ds of wall clock and was "
                                    "killed" % RUN_TIMEOUT, "stderr": ""}
    except OSError as e:
        return {"ok": False, "why": "could not execute: %s" % e, "stderr": ""}
    if p.returncode != 0:
        why = "exited with status %d" % p.returncode
        if p.returncode < 0:
            why = "killed by signal %d" % -p.returncode
        return {"ok": False, "why": why, "exit": p.returncode,
                "stderr": (p.stderr or "") + (p.stdout or "")}
    return {"ok": True, "exit": 0, "stdout": p.stdout or "", "stderr": p.stderr or ""}


def build_and_run(source_text, keep_dir=None, cxx=None):
    workdir = keep_dir or tempfile.mkdtemp(prefix="probe-jeep-")
    os.makedirs(workdir, exist_ok=True)
    prepare(workdir)
    c = compile_cpp(source_text, workdir, cxx=cxx)
    if not c["ok"]:
        return workdir, c, {"ok": False, "why": "not run: compilation failed"}
    r = run_exe(c["exe"], workdir)
    return workdir, c, r


def cleanup(workdir):
    shutil.rmtree(workdir, ignore_errors=True)
