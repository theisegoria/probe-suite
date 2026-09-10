"""Compile and run a single self-contained source file.

Everything happens in a scratch directory that the program is expected to
write its artifacts into, so a run is inspectable afterwards and one task
cannot see another's output.
"""

import os
import shutil
import subprocess
import tempfile

DEFAULT_CXX = os.environ.get("PROBE_CXX", "c++")
COMPILE_TIMEOUT = 120
RUN_TIMEOUT = 60


def compile_cpp(source_text, workdir, std="c++23", cxx=None, extra=()):
    src = os.path.join(workdir, "main.cpp")
    with open(src, "w", encoding="utf-8") as fh:
        fh.write(source_text)
    exe = os.path.join(workdir, "prog")
    cmd = [cxx or DEFAULT_CXX, "-std=" + std, "-O2", src, "-o", exe] + list(extra)
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
        return {"ok": False, "why": "program timed out after %ds" % RUN_TIMEOUT, "stderr": ""}
    except OSError as e:
        return {"ok": False, "why": "could not execute: %s" % e, "stderr": ""}
    if p.returncode != 0:
        why = "exited with status %d" % p.returncode
        if p.returncode < 0:
            why = "killed by signal %d" % -p.returncode
        return {"ok": False, "why": why, "exit": p.returncode,
                "stderr": (p.stderr or "") + (p.stdout or "")}
    return {"ok": True, "exit": 0, "stdout": p.stdout or "", "stderr": p.stderr or ""}


def build_and_run(source_text, keep_dir=None, **kw):
    workdir = keep_dir or tempfile.mkdtemp(prefix="probe-mesh-")
    os.makedirs(workdir, exist_ok=True)
    c = compile_cpp(source_text, workdir, **kw)
    if not c["ok"]:
        return workdir, c, {"ok": False, "why": "not run: compilation failed"}
    r = run_exe(c["exe"], workdir)
    return workdir, c, r


def cleanup(workdir):
    shutil.rmtree(workdir, ignore_errors=True)
