#!/usr/bin/env python3
"""dockerbuild.py - helper to run ESP-Miner builds inside a Docker/Podman container.

Prerequisites (choose one):
  • Podman (rootless-friendly OCI runtime).
    Ubuntu/Debian:  `sudo apt install podman`
    Arch Linux   :  `sudo pacman -S podman`
    macOS (brew) :  `brew install podman`
  • Docker (classic container runtime).
    Ubuntu/Debian:  `sudo apt install docker.io`
    Arch Linux   :  `sudo pacman -S docker`
    macOS (brew) :  `brew install --cask docker`  # Docker Desktop

Make sure the chosen runtime is on your PATH; the script will probe for Podman
first and fall back to Docker.

Usage examples:
  # Run a build in a fresh transient container (default image & cmd)
  ./dockerbuild.py

  # Exec `idf.py clean` inside a container
  ./dockerbuild.py idf.py clean

  # Start a container with a custom image and command
  ./dockerbuild.py --image ghcr.io/espressif/idf:release-v5.4 idf.py menuconfig
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import uuid
import sys
from typing import List


def find_container_runtime() -> str:
    """Return the first available container runtime (`podman` or `docker`)."""
    for runtime in ("podman", "docker"):
        if shutil.which(runtime):
            return runtime
    sys.stderr.write("Error: neither podman nor docker was found in PATH\n")
    sys.exit(1)


def build_container_cmd(runtime: str, name: str, image: str, workspace: str) -> List[str]:
    """Return docker/podman run command prefix for a fresh container started with the host UID/GID."""
    uid = os.getuid()
    gid = os.getgid()
    return [
        runtime,
        "run",
        "--rm",
        "-it",
        "--name",
        name,
        "--user",
        f"{uid}:{gid}",
        "-v",
        f"{workspace}:{workspace}:rw,z",
        "-w",
        workspace,
        image,
    ]


def main():
    parser = argparse.ArgumentParser(description="Run build inside Docker/Podman container")
    parser.add_argument("build_cmd", nargs=argparse.REMAINDER, help="Build command to run (default: idf.py build)")
    parser.add_argument("--image", help="Container image to use",
                    default="ghcr.io/bitaxeorg/esp-miner/devcontainer:sha-6a7c499a5dd8f985a578a05e04eba3fa9f93f1f7")
    # Determine repository root to use as default workspace. Try git, fallback to script parent directory.
    script_dir = os.path.abspath(os.path.dirname(__file__))
    try:
        repo_root = (
            subprocess.check_output(["git", "rev-parse", "--show-toplevel"], cwd=script_dir, text=True)
            .strip()
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        repo_root = os.path.abspath(os.path.join(script_dir, os.pardir))

    parser.add_argument(
        "--workspace",
        help="Path to project root mounted inside container (default: repository root)",
        default=repo_root,
    )

    args = parser.parse_args()

    build_cmd = args.build_cmd or ["idf.py", "build"]

    runtime = find_container_runtime()
    name = f"esp-miner-{uuid.uuid4().hex[:8]}"
    prefix = build_container_cmd(runtime, name, args.image, args.workspace)

    cmd = prefix + build_cmd
    print("Running:", " ".join(cmd))
    os.execvp(cmd[0], cmd)  # replace current process


if __name__ == "__main__":
    main()
