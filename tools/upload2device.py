#!/usr/bin/env python3
"""
upload2device.py
=================
Upload ESP-Miner firmware (``esp-miner.bin``) and optionally a custom web UI archive (``www.bin``)
located in the local ``build/`` directory to one or more ESP-Miner devices over
HTTP OTA endpoints defined in ``main/http_server/openapi.yaml``:

* ``POST /api/system/OTA``    (binary firmware)
* ``POST /api/system/OTAWWW`` (binary web interface)

Usage examples
--------------
1. Upload firmware only (default, since Web UI is embedded):

    $ python3 upload2device.py 192.168.1.50 192.168.1.51

2. Upload custom Web UI partition in addition to firmware:

    $ python3 upload2device.py --custom-www 192.168.1.50

3. Upload custom Web UI partition ONLY (skipping firmware):

    $ python3 upload2device.py --www-only 192.168.1.50

4. Provide IPs via a file and override build directory:

    $ python3 upload2device.py --file devices.txt --build-dir /tmp/build

The script prints a concise status line for every upload and exits with a non-
zero status if any of the uploads failed.
"""
from __future__ import annotations

import argparse
import pathlib
import sys
import textwrap
from typing import Iterable, List

import requests
from requests.exceptions import RequestException

# Default relative build directory containing firmware artifacts
# Determine repository root as the parent directory of this script's directory
SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent  # tools/ is directly under repo root

_FIRMWARE_BIN = "esp-miner.bin"
_WWW_BIN = "www.bin"

# OTA endpoints (relative to http://<device_ip>)
_ENDPOINT_FIRMWARE = "/api/system/OTA"
_ENDPOINT_WWW = "/api/system/OTAWWW"

# HTTP headers – per OpenAPI spec only the content-type is required
_HEADERS = {"Content-Type": "application/octet-stream"}


def _iter_ips(args: argparse.Namespace) -> Iterable[str]:
    """Yield device IP addresses from CLI positional args and/or file."""
    seen: set[str] = set()

    # First, any IPs passed positionally
    for ip in args.device_ips:
        ip = ip.strip()
        if ip and ip not in seen:
            seen.add(ip)
            yield ip

    # Second, any IPs read from --file
    if args.file:
        with open(args.file, "r", encoding="utf-8") as fp:
            for line in fp:
                ip = line.strip()
                if ip and ip not in seen:
                    seen.add(ip)
                    yield ip


def _upload_binary(ip: str, bin_path: pathlib.Path, endpoint: str) -> bool:
    """Upload *bin_path* to *ip* at *endpoint*. Return True on HTTP 200."""
    url = f"http://{ip}{endpoint}"
    try:
        with open(bin_path, "rb") as f:
            resp = requests.post(url, data=f, headers=_HEADERS, timeout=120)
        if resp.status_code == 200:
            print(f"[OK] {bin_path.name} uploaded to {ip}{endpoint}")
            return True
        print(
            f"[FAIL] {bin_path.name} to {ip}{endpoint}: HTTP {resp.status_code} – {resp.text[:100]}",
            file=sys.stderr,
        )
    except (FileNotFoundError, PermissionError) as e:
        print(f"[ERROR] Cannot read {bin_path}: {e}", file=sys.stderr)
    except RequestException as e:
        print(f"[ERROR] Upload to {ip}{endpoint} failed: {e}", file=sys.stderr)
    return False


def _process_device(
    ip: str,
    build_dir: pathlib.Path,
    custom_www: bool = False,
    www_only: bool = False,
) -> bool:
    """Upload web UI (if requested or www-only) and/or firmware to *ip*. Return True if successful."""
    www_path = build_dir / _WWW_BIN
    
    if custom_www or www_only:
        if not www_path.is_file():
            flag_name = "--www-only" if www_only else "--custom-www"
            print(f"[ERROR] {www_path.name} not found in build directory, but {flag_name} was requested.", file=sys.stderr)
            return False
        www_ok = _upload_binary(ip, www_path, _ENDPOINT_WWW)
        if not www_ok:
            return False
        if www_only:
            return True
        # Give device a moment to process first upload
        import time
        time.sleep(1)

    firmware_ok = _upload_binary(ip, build_dir / _FIRMWARE_BIN, _ENDPOINT_FIRMWARE)
    return firmware_ok


def _parse_args(argv: List[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="upload2device.py",
        description="Upload esp-miner firmware and optional web UI to ESP-Miner devices over HTTP OTA.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(
            """Examples:
  python3 upload2device.py 192.168.1.50 192.168.1.51
  python3 upload2device.py --file devices.txt
  python3 upload2device.py --build-dir /tmp/build --custom-www 192.168.1.100
  python3 upload2device.py --www-only 192.168.1.100
""",
        ),
    )

    parser.add_argument(
        "device_ips",
        nargs="*",
        metavar="IP",
        help="IP address(es) of ESP-Miner devices",
    )
    parser.add_argument(
        "--file",
        type=pathlib.Path,
        help="Path to text file containing one device IP per line (optional).",
    )
    parser.add_argument(
        "--build-dir",
        type=pathlib.Path,
        default=None,
        help="Custom build directory containing firmware binaries (default: <repo_root>/build)",
    )
    
    upload_group = parser.add_mutually_exclusive_group()
    upload_group.add_argument(
        "--custom-www",
        action="store_true",
        help="Upload both the custom web UI partition image (www.bin) and firmware (esp-miner.bin)",
    )
    upload_group.add_argument(
        "--www-only",
        action="store_true",
        help="Upload ONLY the custom web UI partition image (www.bin) and skip the firmware",
    )
    return parser.parse_args(argv)


def main(argv: List[str] | None = None) -> None:
    args = _parse_args(argv)

    ips = list(_iter_ips(args))
    if not ips:
        print("No device IPs provided.", file=sys.stderr)
        sys.exit(1)

    # Resolve build directory
    if args.build_dir is None:
        build_dir = (REPO_ROOT / "build").resolve()
    else:
        build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        print(f"Build directory '{build_dir}' does not exist or is not a directory.", file=sys.stderr)
        sys.exit(1)

    overall_success = True
    for ip in ips:
        print(f"\n=== Processing device {ip} ===")
        success = _process_device(ip, build_dir, args.custom_www, args.www_only)
        overall_success = overall_success and success

    sys.exit(0 if overall_success else 2)


if __name__ == "__main__":
    main()
