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


def find_compiler():
    """A compiler that builds for this PC. The ESP-IDF environment puts a clang that
    targets the chip first on the path, so check what each candidate builds for."""
    candidates = [os.environ.get("CC"), "C:/Program Files/LLVM/bin/clang.exe", "clang", "gcc"]
    for candidate in candidates:
        path = candidate and shutil.which(candidate)
        if not path:
            continue
        target = subprocess.run([path, "-dumpmachine"], capture_output=True, text=True).stdout
        if target and not any(word in target for word in ("riscv", "xtensa", "esp")):
            return path, target
    sys.exit("No C compiler for this PC found. Install clang or gcc, or set CC.")


def build(name, sources):
    compiler, target = find_compiler()
    # A compiler targeting MSVC has its maths in the C library, and rejects -lm.
    libs = [] if "msvc" in target else ["-lm"]
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
    if not build("replay", [os.path.join(HOST, "replay.c"), *core_sources]):
        failed.append("replay (build)")
    for test in tests:
        name = os.path.basename(test)[:-2]
        print(f"== {name}")
        if not build(name, [test, *core_sources, *helpers]):
            failed.append(name + " (build)")
            continue
        if subprocess.run([exe_name(name)], cwd=ROOT).returncode != 0:
            failed.append(name)
    print()
    if failed:
        print("FAILED: " + ", ".join(failed))
        return 1
    print(f"All {len(tests)} test programs passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
