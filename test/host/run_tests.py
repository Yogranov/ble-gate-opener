#!/usr/bin/env python3
"""Build and run the logic tests on this PC. Needs clang or gcc, no ESP-IDF.

    python test/host/run_tests.py              run everything
    python test/host/run_tests.py trigger      run test_trigger.c only
"""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CORE = os.path.join(ROOT, "components", "yg_core")
HOST = os.path.join(ROOT, "test", "host")
OUT = os.path.join(ROOT, "build-host")

FLAGS = ["-std=c17", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
         "-Wno-deprecated-declarations", "-D_CRT_SECURE_NO_WARNINGS", "-I", os.path.join(CORE, "include"), "-I", HOST]


def exe_name(name):
    return os.path.join(OUT, name + (".exe" if os.name == "nt" else ""))


def build(name, sources):
    compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        sys.exit("No C compiler found. Install clang or gcc, or set CC.")
    libs = ["-lm"] if os.name != "nt" else []
    cmd = [compiler, *FLAGS, *sources, "-o", exe_name(name), *libs]
    result = subprocess.run(cmd)
    return result.returncode == 0


def main():
    os.makedirs(OUT, exist_ok=True)
    wanted = sys.argv[1:]
    core_sources = sorted(glob.glob(os.path.join(CORE, "*.c")))
    helpers = sorted(glob.glob(os.path.join(HOST, "fake_*.c")))
    tests = sorted(glob.glob(os.path.join(HOST, "test_*.c")))
    if wanted:
        tests = [t for t in tests if os.path.basename(t)[5:-2] in wanted]
    failed = []
    for test in tests:
        name = os.path.basename(test)[:-2]
        print(f"== {name}")
        if not build(name, [test, *core_sources, *helpers]):
            failed.append(name + " (build)")
            continue
        if subprocess.run([exe_name(name)]).returncode != 0:
            failed.append(name)
    replay = os.path.join(HOST, "replay.c")
    if os.path.exists(replay) and not wanted:
        if not build("replay", [replay, *core_sources]):
            failed.append("replay (build)")
    print()
    if failed:
        print("FAILED: " + ", ".join(failed))
        return 1
    print(f"All {len(tests)} test programs passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
