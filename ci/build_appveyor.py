#!/usr/bin/env python3

"""
Build script for AppVeyor CI.

Runs on Windows, planned to support Linux and Mac as well.
If using MSVC, expects environment to have been set up by vcvarsall.bat.
Calls CMake.

Receives input via environment variables set via the environment matrix,
not command line arguments.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import typing as T
from contextlib import contextmanager
from pathlib import Path


@contextmanager
def pushd(new_dir: T.Union[Path, str]) -> T.Iterator[None]:
    previous_dir = os.getcwd()
    os.chdir(str(new_dir))
    try:
        yield
    finally:
        os.chdir(previous_dir)


def sanitize_path(filename: str) -> str:
    """Replace all characters except alphanumerics and separators with underscores."""
    return re.sub(r"[^a-zA-Z0-9_\-.]", r"_", filename)


def quote_str(path: Path) -> str:
    return shlex.quote(str(path))


def run(*strings: T.List[str], **kwargs):
    # fmt: off
    args = [arg
        for s in strings
        for arg in shlex.split(s)
    ]
    # fmt: on
    subprocess.run(args, check=True, **kwargs)


def parse_bool_int(s: str) -> T.Optional[bool]:
    return {"": False, "0": False, "1": True}.get(s, None)


"""Which VM and build configuration (32/64, compiler, target OS) is being used."""
APPVEYOR_JOB_NAME = os.environ.get("APPVEYOR_JOB_NAME", "APPVEYOR_JOB_NAME")

"""Debug or release build."""
CONFIGURATION = os.environ.get("CONFIGURATION", "Release")

"""Disable precompiled headers to expose missing #includes."""
DISABLE_PCH = os.environ.get("DISABLE_PCH", "0")
DISABLE_PCH = parse_bool_int(DISABLE_PCH)
if DISABLE_PCH is None:
    raise ValueError("DISABLE_PCH environment value must be 0 or 1 if present")

"""Version string."""
APPVEYOR_BUILD_VERSION = os.environ.get("APPVEYOR_BUILD_VERSION", "UnknownVer")


def ARCHIVE():
    """Output archive file name."""

    configuration = "" if CONFIGURATION == "Release" else f"-{CONFIGURATION}"

    # TODO indicate 32/64 and compiler/OS
    return f"exotracker-v{APPVEYOR_BUILD_VERSION}{configuration}-dev"


ARCHIVE = ARCHIVE()


def resolve_compilers():
    """CMake expects CC and CXX environment variables to be absolute compiler paths."""
    for compiler in ["CC", "CXX"]:
        path = os.environ[compiler]
        os.environ[compiler] = shutil.which(path)


BUILD_DIR = sanitize_path(f"build-{APPVEYOR_JOB_NAME}-{CONFIGURATION}")


def build():
    CMAKE_USER_BEGIN = Path("cmake_user_begin.cmake").resolve()

    resolve_compilers()

    if DISABLE_PCH:
        with CMAKE_USER_BEGIN.open("a") as f:
            f.write("set(USE_PCH FALSE)\n")

    os.makedirs(BUILD_DIR, exist_ok=True)
    os.chdir(BUILD_DIR)

    run(f"cmake .. -DCMAKE_BUILD_TYPE={CONFIGURATION} -G Ninja")
    run("ninja")


def test():
    os.chdir(BUILD_DIR)
    run("exotracker-tests")


ARCHIVE_ROOT = "archive-root"
EXE_NAME = "exotracker-qt"


def archive():
    root_dir = Path().resolve()
    build_dir = Path(BUILD_DIR).resolve()
    archive_name = os.path.abspath(ARCHIVE)

    shutil.rmtree(ARCHIVE_ROOT, ignore_errors=True)
    os.mkdir(ARCHIVE_ROOT)

    def copy_to_cwd(in_file: str):
        """Copies a file to the current directory without renaming it.
        Copying instead of renaming makes archive() idempotent
        and simplifies local testing."""
        shutil.copy(str(build_dir / in_file), Path(in_file).name)

    with pushd(ARCHIVE_ROOT):
        # TODO branch on OS

        copy_to_cwd(f"{EXE_NAME}.exe")

        # Archive Qt DLLs.
        run(
            "windeployqt.exe",
            f"{EXE_NAME}.exe",
            # "--verbose 2",
            # Reduce file size.
            "--no-compiler-runtime --no-angle --no-opengl-sw",
        )

        # Remove unnecessary Qt code.
        for path in Path("imageformats").iterdir():
            if not path.name.startswith("qsvg"):
                path.unlink()

        # Create archive (CI artifact).
        run("7z a -mx=3", shlex.quote(archive_name + ".7z"), ".")

    # Visual Studio will not load .pdb files which have been renamed.
    # So give the archive a different name, but preserve the name of the .pdb.
    run(
        "7z a -mx=3",
        shlex.quote(archive_name + ".pdb.7z"),
        quote_str(build_dir / f"{EXE_NAME}.pdb"),
    )


class DefaultHelpParser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write("error: %s\n" % message)
        self.print_help(sys.stderr)
        sys.exit(2)


def main():
    # create the top-level parser
    parser = DefaultHelpParser()

    def f():
        subparsers = parser.add_subparsers(dest="cmd")
        subparsers.required = True

        parser_build = subparsers.add_parser("build")
        parser_test = subparsers.add_parser("test")
        parser_archive = subparsers.add_parser("archive")

    f()
    args = parser.parse_args()

    if args.cmd == "build":
        build()

    if args.cmd == "test":
        test()

    if args.cmd == "archive":
        archive()


if __name__ == "__main__":
    main()
