#!/usr/bin/env python3
"""Build, verify and archive a libnmo release package.

The package is the CMake install tree of a static Release build: the nmo CLI,
libnmo with its headers, the CMake package and pkg-config files, runtime data,
shell completions, and the licenses of libnmo and its bundled dependencies.

Before archiving, the package is checked from the outside: the CLI runs from
the package, its completions match the installed files, it finds
its runtime data from any working directory (with --sample), and
tests/package_consumer builds and runs against the package with
find_package(libnmo).  The archive is a .zip for Windows platforms and a
.tar.gz elsewhere, written next to a .sha256 file.

Extra CMake arguments after "--" go to both the libnmo and the consumer
configure step, e.g. "-- -DCMAKE_OSX_ARCHITECTURES=arm64;x86_64".
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION_HEADER = ROOT / "include" / "nmo_types.h"
CONSUMER_SOURCE = ROOT / "tests" / "package_consumer"
COMPLETIONS = {
    "bash": "nmo.bash",
    "fish": "nmo.fish",
    "zsh": "_nmo",
    "powershell": "nmo.ps1",
}
DATA_FILE = Path("share") / "libnmo" / "data" / "virtools_plugins.json"


class PackageError(Exception):
    pass


def step(name: str) -> None:
    print(f"==> {name}", flush=True)


def run(*command: str | Path, cwd: Path = ROOT, env: dict[str, str] | None = None,
        capture: bool = False) -> str:
    args = [str(part) for part in command]
    result = subprocess.run(args, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE if capture else None)
    if result.returncode != 0:
        raise PackageError(f"command failed ({result.returncode}): {' '.join(args)}")
    return result.stdout if capture else ""


def read_version() -> str:
    text = VERSION_HEADER.read_text(encoding="utf-8")
    parts = []
    for name in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"^#define NMO_VERSION_{name} (\d+)$", text, re.M)
        if not match:
            raise PackageError(f"NMO_VERSION_{name} not found in {VERSION_HEADER}")
        parts.append(match.group(1))
    return ".".join(parts)


def remove_tree(path: Path, parent: Path) -> None:
    """Delete path, refusing anything that is not strictly inside parent."""
    if not path.exists():
        return
    if parent.resolve() not in path.resolve().parents:
        raise PackageError(f"refusing to delete {path} outside {parent}")
    if path.is_dir():
        shutil.rmtree(path)
    else:
        path.unlink()


def configure_build_test(args: argparse.Namespace, build_dir: Path) -> None:
    step("Configure Release build")
    run("cmake", "-S", ROOT, "-B", build_dir, "-G", args.generator,
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DNMO_BUILD_TESTS={'OFF' if args.skip_tests else 'ON'}",
        "-DNMO_BUILD_TOOLS=ON",
        "-DNMO_BUILD_EXAMPLES=OFF",
        "-DNMO_BUILD_SHARED=OFF",
        *args.cmake_args)

    step("Build")
    run("cmake", "--build", build_dir, "--config", "Release", "--parallel")

    if not args.skip_tests:
        step("Run tests")
        run("ctest", "--test-dir", build_dir, "--build-config", "Release",
            "--output-on-failure", "--parallel", str(os.cpu_count() or 2),
            "--label-exclude", "performance")


def install(build_dir: Path, package_root: Path, dist_dir: Path) -> None:
    step("Install package tree")
    remove_tree(package_root, dist_dir)
    run("cmake", "--install", build_dir, "--config", "Release", "--prefix", package_root)
    files = sorted(path.relative_to(package_root).as_posix()
                   for path in package_root.rglob("*") if path.is_file())
    (package_root / "MANIFEST.txt").write_text("\n".join(files) + "\n", encoding="utf-8",
                                               newline="\n")


def verify_cli(package_root: Path, sample: Path | None, forbidden_imports: list[str]) -> None:
    step("Verify CLI")
    exe = package_root / "bin" / ("nmo.exe" if os.name == "nt" else "nmo")
    if not exe.is_file():
        raise PackageError(f"missing {exe}")
    if not (package_root / DATA_FILE).is_file():
        raise PackageError(f"missing {DATA_FILE.as_posix()}")
    run(exe, "--version")
    run(exe, "--help", capture=True)

    for shell, name in COMPLETIONS.items():
        installed = package_root / "share" / "completions" / name
        if not installed.is_file():
            raise PackageError(f"missing completion file {installed}")
        generated = run(exe, "completion", shell, capture=True)
        if generated.rstrip("\r\n") != installed.read_text(encoding="utf-8").rstrip("\r\n"):
            raise PackageError(f"nmo completion {shell} does not match {installed}")

    if sample is not None:
        # Run away from the source tree and without NMO_DATA_DIR, so the CLI
        # has to find share/libnmo/data relative to its own location.
        env = {key: value for key, value in os.environ.items() if key != "NMO_DATA_DIR"}
        output = run(exe, "-f", "json", "file", "plugins", sample.resolve(),
                     cwd=package_root, env=env, capture=True)
        if not re.search(r'"missing_count"\s*:\s*0\b', output):
            raise PackageError("packaged CLI reports missing plugin dependencies")

    if forbidden_imports:
        objdump = shutil.which("objdump")
        if objdump is None:
            raise PackageError("objdump is required to check --forbid-import")
        imports = run(objdump, "-p", exe, capture=True).lower()
        for name in forbidden_imports:
            if name.lower() in imports:
                raise PackageError(f"{exe.name} imports {name}")


def verify_consumer(args: argparse.Namespace, package_root: Path, build_dir: Path) -> None:
    step("Build consumer against the package")
    consumer_dir = build_dir / "_package_consumer"
    remove_tree(consumer_dir, build_dir)
    run("cmake", "-S", CONSUMER_SOURCE, "-B", consumer_dir, "-G", args.generator,
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={package_root.as_posix()}",
        *args.cmake_args)
    run("cmake", "--build", consumer_dir, "--config", "Release")
    name = "package_consumer.exe" if os.name == "nt" else "package_consumer"
    candidates = [consumer_dir / name, consumer_dir / "Release" / name]
    exe = next((path for path in candidates if path.is_file()), None)
    if exe is None:
        raise PackageError(f"consumer executable not found in {consumer_dir}")
    run(exe)


def archive(package_root: Path, dist_dir: Path, use_zip: bool) -> Path:
    step("Create archive")
    suffix = ".zip" if use_zip else ".tar.gz"
    archive_path = dist_dir / f"{package_root.name}{suffix}"
    remove_tree(archive_path, dist_dir)
    files = sorted(path for path in package_root.rglob("*") if path.is_file())
    if use_zip:
        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as out:
            for path in files:
                out.write(path, path.relative_to(dist_dir).as_posix())
    else:
        with tarfile.open(archive_path, "w:gz") as out:
            for path in files:
                out.add(path, path.relative_to(dist_dir).as_posix(), recursive=False)

    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    checksum = archive_path.with_name(archive_path.name + ".sha256")
    checksum.write_text(f"{digest}  {archive_path.name}\n", encoding="utf-8", newline="\n")
    return archive_path


def main(argv: list[str]) -> int:
    if "--" in argv:
        split = argv.index("--")
        argv, cmake_args = argv[:split], argv[split + 1:]
    else:
        cmake_args = []

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", required=True,
                        help="platform label in the package name, e.g. linux-x64")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-package",
                        help="build directory (default: build-package)")
    parser.add_argument("--dist-dir", type=Path, default=ROOT / "dist",
                        help="output directory (default: dist)")
    parser.add_argument("--generator", default="Ninja", help="CMake generator (default: Ninja)")
    parser.add_argument("--skip-tests", action="store_true", help="do not build or run tests")
    parser.add_argument("--sample", type=Path,
                        help="Virtools file whose plugin dependencies must all resolve")
    parser.add_argument("--forbid-import", action="append", default=[], metavar="DLL",
                        help="fail if the Windows CLI imports this DLL (needs objdump)")
    args = parser.parse_args(argv)
    args.cmake_args = cmake_args

    build_dir = args.build_dir.resolve()
    dist_dir = args.dist_dir.resolve()
    try:
        version = read_version()
        package_root = dist_dir / f"libnmo-{version}-{args.platform}"
        dist_dir.mkdir(parents=True, exist_ok=True)
        configure_build_test(args, build_dir)
        install(build_dir, package_root, dist_dir)
        verify_cli(package_root, args.sample, args.forbid_import)
        verify_consumer(args, package_root, build_dir)
        archive_path = archive(package_root, dist_dir, args.platform.startswith("windows"))
    except PackageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Package created: {archive_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
