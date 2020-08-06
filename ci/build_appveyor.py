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


def run(*strings: T.List[str], **kwargs):
    # fmt: off
    args = [arg
        for s in strings
        for arg in shlex.split(s)
    ]
    # fmt: on
    subprocess.run(args, check=True, **kwargs)


"""Which VM and build configuration (32/64, compiler, target OS) is being used."""
APPVEYOR_JOB_NAME = os.environ.get("APPVEYOR_JOB_NAME", "APPVEYOR_JOB_NAME")

"""Debug or release build."""
CONFIGURATION = os.environ.get("CONFIGURATION", "Release")

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
    resolve_compilers()

    os.mkdir(BUILD_DIR)
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

    def move_to_cwd(in_file: str):
        """Moves a file to the current directory without renaming it."""
        (build_dir / in_file).rename(Path(in_file).name)

    with pushd(ARCHIVE_ROOT):
        # TODO branch on OS

        move_to_cwd(f"{EXE_NAME}.exe")

        # Archive Qt DLLs.
        run(
            "windeployqt.exe",
            f"{EXE_NAME}.exe",
            # "--verbose 2",
            # Reduce file size.
            "--no-compiler-runtime --no-svg --no-angle --no-opengl-sw",
        )

        # Remove unnecessary Qt code.
        shutil.rmtree("imageformats")

        # Create archive (CI artifact).
        run("7z a -mx=3", shlex.quote(archive_name + ".7z"), ".")

    # Move .pdb file to project root instead of archive root.
    (build_dir / f"{EXE_NAME}.pdb").rename(archive_name + ".pdb")


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
