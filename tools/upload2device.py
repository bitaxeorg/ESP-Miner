#!/usr/bin/env python3
"""
upload2device.py
=================
Upload ESP-Miner firmware (``esp-miner.bin``) and optionally a custom web UI archive (``www.bin``)
located in the local ``build/`` directory to one or more ESP-Miner devices over
HTTP OTA endpoints defined in ``main/http_server/openapi.yaml``:

* ``POST /api/system/OTA``    (binary firmware)
* ``POST /api/system/OTAWWW`` (binary web interface)

Features:
---------
* **Staggered Parallel Uploads**: Updates multiple devices concurrently with a configurable
  stagger delay to avoid 2.4 GHz Wi-Fi saturation and simultaneous reboot current surges.
* **Live Progress Tracking**: Native multi-device terminal progress bars (speed, percentage,
  bytes, status) using standard library ANSI escape codes with zero extra dependencies.
* **Aligned & Throttled UI**: Column-aligned progress bars, throttled terminal redraws (2-3 Hz)
  for smooth rendering, and per-device average speeds/timings on completion.
* **Pure Stdlib + Requests**: No external UI packages (like ``rich`` or ``tqdm``) required.
* **Graceful Logging Fallback**: Automatically switches to milestone text logs when piped,
  redirected to a file, or when ``--plain`` is specified.

Usage examples
--------------
1. Upload firmware to multiple devices (default: 4 concurrent workers, 3s stagger):

    $ python3 upload2device.py 192.168.1.50 192.168.1.51 192.168.1.52

2. Custom concurrency and stagger delay:

    $ python3 upload2device.py --workers 2 --stagger 5.0 192.168.1.50 192.168.1.51

3. Sequential execution (equivalent to legacy behavior):

    $ python3 upload2device.py --workers 1 192.168.1.50 192.168.1.51

4. Upload custom Web UI partition in addition to firmware:

    $ python3 upload2device.py --custom-www 192.168.1.50

5. Upload custom Web UI partition ONLY (skipping firmware):

    $ python3 upload2device.py --www-only 192.168.1.50

6. Provide IPs via a file and override build directory:

    $ python3 upload2device.py --file devices.txt --build-dir /tmp/build

7. Force plain-text logging (disables ANSI progress bars):

    $ python3 upload2device.py --plain 192.168.1.50 192.168.1.51
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, wait
import os
import pathlib
import shutil
import sys
import textwrap
import threading
import time
from typing import Callable, Iterable, List

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

# Enable ANSI escape sequences on Windows 10/11 if available
if sys.platform == "win32":
    os.system("")


class _UploadAborted(Exception):
    """Raised internally to abort an in-progress streaming upload on a second Ctrl+C."""


class ProgressFileReader:
    """File wrapper that counts bytes read and triggers a callback for progress tracking."""

    def __init__(
        self,
        filepath: pathlib.Path,
        callback: Callable[[int], None] | None = None,
        abort_event: threading.Event | None = None,
    ) -> None:
        self._f = open(filepath, "rb")
        self.total_size = filepath.stat().st_size
        self.bytes_read = 0
        self.callback = callback
        self.abort_event = abort_event

    def read(self, size: int = -1) -> bytes:
        if self.abort_event and self.abort_event.is_set():
            raise _UploadAborted()
        chunk = self._f.read(size)
        if chunk:
            self.bytes_read += len(chunk)
            if self.callback:
                self.callback(len(chunk))
        return chunk

    def __len__(self) -> int:
        return self.total_size

    def __enter__(self) -> ProgressFileReader:
        return self

    def __exit__(self, *args: object) -> None:
        self._f.close()


class StaggerCoordinator:
    """Ensures a minimum time gap between the launch of consecutive device uploads."""

    def __init__(self, delay_seconds: float, cancel_event: threading.Event | None = None) -> None:
        self.delay = max(0.0, delay_seconds)
        self.cancel_event = cancel_event
        self.next_start_time = 0.0
        self.lock = threading.Lock()

    def wait(self, on_wait: Callable[[float, float], None] | None = None) -> bool:
        """Wait until this worker's staggered start slot.

        Calls *on_wait(remaining_seconds, progress_pct)* periodically while waiting.
        Returns False if cancelled before the slot is reached, True otherwise.
        """
        if self.cancel_event and self.cancel_event.is_set():
            return False
        if self.delay <= 0.0:
            return True

        with self.lock:
            now = time.monotonic()
            if self.next_start_time == 0.0 or now >= self.next_start_time:
                self.next_start_time = now + self.delay
                return True
            target_start = self.next_start_time
            self.next_start_time = target_start + self.delay

        total_wait = target_start - now
        while True:
            if self.cancel_event and self.cancel_event.is_set():
                return False
            remaining = target_start - time.monotonic()
            if remaining <= 0.0:
                if on_wait:
                    on_wait(0.0, 100.0)
                return True
            pct = min(100.0, max(0.0, (1.0 - remaining / total_wait) * 100.0))
            if on_wait:
                on_wait(remaining, pct)
            step = min(0.1, remaining)
            if self.cancel_event:
                if self.cancel_event.wait(timeout=step):
                    return False
            else:
                time.sleep(step)


class TerminalDashboard:
    """Multi-device progress dashboard rendered via standard ANSI escape codes."""

    def __init__(
        self,
        devices: list[str],
        totals: dict[str, int],
        is_plain: bool = False,
    ) -> None:
        self.devices = devices
        self.totals = totals
        self.is_tty = sys.stdout.isatty() and not is_plain
        self.lock = threading.Lock()
        self.last_redraw = 0.0
        self.footer: str | None = None
        self._rendered_lines = len(devices)

        # Calculate aligned column width for device names (min 16, max 32)
        max_name_len = max((len(d) for d in devices), default=16)
        self.dev_width = min(32, max(16, max_name_len))

        self.state: dict[str, dict] = {
            dev: {
                "sent": 0,
                "status": "Queued",
                "stagger_pct": None,
                "start_time": None,
                "end_time": None,
                "done": False,
                "ok": False,
                "last_logged_pct": -1,
            }
            for dev in devices
        }

        if self.is_tty:
            for _ in devices:
                sys.stdout.write("\n")
            sys.stdout.flush()
            self._redraw(force=True)
        else:
            print(f"Updating {len(devices)} device(s)...", flush=True)

    def update(
        self,
        dev: str,
        advance: int = 0,
        status: str | None = None,
        stagger_pct: float | None = None,
        done: bool = False,
        ok: bool = False,
        throttle: bool = False,
    ) -> None:
        with self.lock:
            s = self.state[dev]
            force = False

            if stagger_pct is not None:
                s["stagger_pct"] = stagger_pct
            elif status is not None and not status.startswith("Stagger"):
                s["stagger_pct"] = None

            if status is not None and status != s["status"]:
                s["status"] = status
                if not throttle:
                    force = True

            if advance > 0:
                s["sent"] += advance
                s["stagger_pct"] = None
                if s["start_time"] is None:
                    s["start_time"] = time.time()
                    force = True

            if done:
                s["done"] = True
                s["ok"] = ok
                s["stagger_pct"] = None
                s["end_time"] = time.time()
                force = True

            if self.is_tty:
                self._redraw(force=force)
            else:
                self._log_plain(dev, s, done, ok)

    def set_footer(self, text: str | None) -> None:
        with self.lock:
            if self.is_tty:
                self.footer = text
                self._redraw(force=True)
            elif text:
                print(text, file=sys.stderr, flush=True)

    def _log_plain(self, dev: str, s: dict, done: bool, ok: bool) -> None:
        """Periodic milestone logging when running non-interactively."""
        tot = self.totals.get(dev, 1)
        pct = int(min(100.0, (s["sent"] / tot * 100.0))) if tot > 0 else 0
        if done:
            if not ok and s["status"].startswith("Cancelled"):
                print(f"[CANCELLED] {dev}: {s['status']}", flush=True)
                return
            if not ok and s["status"] == "Aborted":
                print(f"[ABORTED] {dev}: Aborted", flush=True)
                return
            dt = (s["end_time"] - s["start_time"]) if s["start_time"] else 0.0
            speed = (s["sent"] / dt) if dt > 0 else 0.0
            speed_str = (
                f"{speed / (1024 * 1024):.1f} MB/s"
                if speed >= 1024 * 1024
                else f"{speed / 1024:.0f} KB/s"
            )
            status_text = f"Complete in {dt:.1f}s ({speed_str})" if ok else f"Failed – {s['status']}"
            tag = "[OK]" if ok else "[FAIL]"
            print(f"{tag} {dev}: {status_text}", flush=True)
        elif s["status"] and pct // 25 > s["last_logged_pct"]:
            s["last_logged_pct"] = pct // 25
            print(f"[{dev}] {s['status']} ({pct}%)", flush=True)

    def _redraw(self, force: bool = False) -> None:
        now = time.time()
        # Throttled refresh: up to 5 times per second (every 0.2s) unless forced
        if not force and (now - self.last_redraw < 0.2):
            return
        self.last_redraw = now

        sys.stdout.write(f"\033[{self._rendered_lines}F")

        for dev in self.devices:
            s = self.state[dev]
            tot = self.totals.get(dev, 1)
            if s["stagger_pct"] is not None and s["sent"] == 0 and not s["done"]:
                pct = min(100.0, max(0.0, s["stagger_pct"]))
            else:
                pct = min(100.0, (s["sent"] / tot * 100.0)) if tot > 0 else 0.0

            # Device label truncated to max 32 if exceptionally long
            dev_label = dev if len(dev) <= self.dev_width else dev[: self.dev_width - 1] + "…"
            is_cancelled = s["done"] and not s["ok"] and s["status"].startswith("Cancelled")
            is_aborted = s["done"] and not s["ok"] and s["status"] == "Aborted"

            if s["done"]:
                icon = "✓" if s["ok"] else "✗"
                if s["ok"]:
                    color = "\033[32m"
                elif is_cancelled:
                    color = "\033[33m"
                else:
                    color = "\033[31m"
            elif s["sent"] > 0:
                icon = "•"
                color = "\033[36m"
            else:
                icon = "•"
                color = "\033[33m"

            reset = "\033[0m"
            dev_prefix = f"{color}{icon} {dev_label:<{self.dev_width}}{reset} "

            bar_len = 12
            filled = int(bar_len * pct / 100)
            bar = "█" * filled + "░" * (bar_len - filled)

            # If not yet uploading (pending / staggering / cancelled before start): show bar + % + status
            if s["sent"] == 0 and (not s["done"] or is_cancelled or is_aborted):
                line = f"{dev_prefix}[{bar}] {pct:>3.0f}% {color}{s['status']}{reset}"
            else:
                mb_sent = s["sent"] / (1024 * 1024)
                mb_tot = tot / (1024 * 1024)
                metrics = f"[{bar}] {pct:>3.0f}% {mb_sent:.1f}/{mb_tot:.1f} MB"

                if s["done"]:
                    if s["ok"]:
                        dt = (s["end_time"] - s["start_time"]) if s["start_time"] else 0.0
                        speed = (s["sent"] / dt) if dt > 0 else 0.0
                        speed_str = (
                            f"{speed / (1024 * 1024):.1f} MB/s"
                            if speed >= 1024 * 1024
                            else f"{speed / 1024:.0f} KB/s"
                        )
                        tail = f" in {dt:.1f}s ({speed_str})"
                    elif is_cancelled or is_aborted:
                        tail = f" {s['status']}"
                    else:
                        tail = f" Failed: {s['status']}"
                else:
                    dt = now - s["start_time"] if s["start_time"] else 0.0
                    if dt > 0.3:
                        speed = (s["sent"] / dt) if dt > 0 else 0.0
                        speed_str = (
                            f"{speed / (1024 * 1024):.1f} MB/s"
                            if speed >= 1024 * 1024
                            else f"{speed / 1024:.0f} KB/s"
                        )
                        tail = f" ({speed_str})"
                    else:
                        tail = ""

                line = f"{dev_prefix}{metrics}{color if s['done'] else ''}{tail}{reset if s['done'] else ''}"

            sys.stdout.write(f"\033[2K\r{line}\n")

        if self.footer is not None:
            sys.stdout.write(f"\033[2K\r\033[33m{self.footer}\033[0m\n")
            self._rendered_lines = len(self.devices) + 1
        elif self._rendered_lines > len(self.devices):
            sys.stdout.write("\033[2K\r")
            self._rendered_lines = len(self.devices)

        sys.stdout.flush()


def _iter_ips(args: argparse.Namespace) -> Iterable[str]:
    """Yield device IP addresses from CLI positional args and/or file."""
    seen: set[str] = set()

    for ip in args.device_ips:
        ip = ip.strip()
        if ip and ip not in seen:
            seen.add(ip)
            yield ip

    if args.file:
        with open(args.file, "r", encoding="utf-8") as fp:
            for line in fp:
                ip = line.strip()
                if ip and ip not in seen:
                    seen.add(ip)
                    yield ip


def _upload_binary(
    ip: str,
    bin_path: pathlib.Path,
    endpoint: str,
    timeout: int = 120,
    on_chunk: Callable[[int], None] | None = None,
    abort_event: threading.Event | None = None,
) -> tuple[bool, str]:
    """Upload *bin_path* to *ip* at *endpoint*. Return (True, 'OK') on HTTP 200."""
    url = f"http://{ip}{endpoint}"
    file_size = bin_path.stat().st_size
    headers = {
        "Content-Type": "application/octet-stream",
        "Content-Length": str(file_size),
    }
    try:
        with ProgressFileReader(bin_path, callback=on_chunk, abort_event=abort_event) as reader:
            resp = requests.post(url, data=reader, headers=headers, timeout=timeout)
        if resp.status_code == 200:
            return True, "OK"
        snippet = resp.text[:100].strip()
        return False, f"HTTP {resp.status_code} – {snippet}"
    except _UploadAborted:
        return False, "Aborted"
    except (FileNotFoundError, PermissionError) as e:
        return False, f"Cannot read {bin_path.name}: {e}"
    except RequestException as e:
        return False, f"Network error: {e}"


def _process_device(
    ip: str,
    build_dir: pathlib.Path,
    custom_www: bool = False,
    www_only: bool = False,
    timeout: int = 120,
    dashboard: TerminalDashboard | None = None,
    cancel_event: threading.Event | None = None,
    abort_event: threading.Event | None = None,
) -> tuple[bool, str]:
    """Upload Web UI and/or firmware to *ip*. Return (success, message)."""
    www_path = build_dir / _WWW_BIN
    fw_path = build_dir / _FIRMWARE_BIN

    def chunk_cb(n: int) -> None:
        if dashboard:
            dashboard.update(ip, advance=n)

    # Step 1: Upload Web UI if requested
    if custom_www or www_only:
        if dashboard:
            dashboard.update(ip, status=f"Uploading {_WWW_BIN}...")
        ok, msg = _upload_binary(
            ip, www_path, _ENDPOINT_WWW, timeout=timeout, on_chunk=chunk_cb, abort_event=abort_event
        )
        if not ok:
            return False, msg if msg == "Aborted" else f"{_WWW_BIN}: {msg}"
        if www_only:
            return True, "OK (Web UI uploaded)"
        if dashboard:
            dashboard.update(ip, status="Waiting 1s...")
        if cancel_event:
            if cancel_event.wait(timeout=1.0):
                return False, f"Cancelled (after {_WWW_BIN})"
        else:
            time.sleep(1.0)

    # Step 2: Upload Firmware
    if dashboard:
        dashboard.update(ip, status=f"Uploading {_FIRMWARE_BIN}...")
    ok, msg = _upload_binary(
        ip, fw_path, _ENDPOINT_FIRMWARE, timeout=timeout, on_chunk=chunk_cb, abort_event=abort_event
    )
    if not ok:
        return False, msg if msg == "Aborted" else f"{_FIRMWARE_BIN}: {msg}"

    return True, "Complete"


def _parse_args(argv: List[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="upload2device.py",
        description="Upload esp-miner firmware and optional web UI to ESP-Miner devices over HTTP OTA.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(
            """Examples:
  python3 upload2device.py 192.168.1.50 192.168.1.51 192.168.1.52
  python3 upload2device.py --workers 2 --stagger 5.0 192.168.1.50 192.168.1.51
  python3 upload2device.py --file devices.txt
  python3 upload2device.py --build-dir /tmp/build --custom-www 192.168.1.100
  python3 upload2device.py --www-only 192.168.1.100
  python3 upload2device.py --plain 192.168.1.50
""",
        ),
    )

    parser.add_argument(
        "device_ips",
        nargs="*",
        metavar="IP",
        help="IP address(es) or hostname(s) of ESP-Miner devices",
    )
    parser.add_argument(
        "--file",
        type=pathlib.Path,
        help="Path to text file containing one device IP/hostname per line (optional).",
    )
    parser.add_argument(
        "--build-dir",
        type=pathlib.Path,
        default=None,
        help="Custom build directory containing firmware binaries (default: <repo_root>/build)",
    )
    parser.add_argument(
        "-w",
        "--workers",
        type=int,
        default=4,
        help="Number of concurrent device uploads (default: 4, set to 1 for sequential)",
    )
    parser.add_argument(
        "-s",
        "--stagger",
        type=float,
        default=3.0,
        help="Delay in seconds between starting each device upload (default: 3.0, set to 0 to disable)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        help="HTTP upload timeout in seconds per binary (default: 120)",
    )
    parser.add_argument(
        "--plain",
        action="store_true",
        help="Use simple plain-text logging instead of ANSI progress bars",
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

    # Validate required binary files exist upfront before starting any upload
    www_path = build_dir / _WWW_BIN
    fw_path = build_dir / _FIRMWARE_BIN

    if (args.custom_www or args.www_only) and not www_path.is_file():
        flag = "--www-only" if args.www_only else "--custom-www"
        print(f"[ERROR] '{www_path.name}' not found in '{build_dir}', but {flag} was requested.", file=sys.stderr)
        sys.exit(1)

    if not args.www_only and not fw_path.is_file():
        print(f"[ERROR] Firmware image '{fw_path.name}' not found in '{build_dir}'. Build the project first.", file=sys.stderr)
        sys.exit(1)

    # Calculate total upload size per device
    www_size = www_path.stat().st_size if (args.custom_www or args.www_only) else 0
    fw_size = fw_path.stat().st_size if not args.www_only else 0
    totals = {ip: (www_size + fw_size) for ip in ips}

    cancel_event = threading.Event()
    abort_event = threading.Event()
    dashboard = TerminalDashboard(ips, totals, is_plain=args.plain)
    coordinator = StaggerCoordinator(args.stagger if len(ips) > 1 else 0.0, cancel_event=cancel_event)

    results: dict[str, tuple[bool, str]] = {}
    max_workers = max(1, min(args.workers, len(ips)))

    def worker(ip: str) -> None:
        if cancel_event.is_set():
            results[ip] = (False, "Cancelled")
            dashboard.update(ip, status="Cancelled", done=True, ok=False)
            return

        first_tick = True

        def on_stagger(remaining: float, pct: float) -> None:
            nonlocal first_tick
            dashboard.update(
                ip,
                status=f"Stagger ({remaining:.1f}s)",
                stagger_pct=pct,
                throttle=not first_tick,
            )
            first_tick = False

        if not coordinator.wait(on_wait=on_stagger) or cancel_event.is_set():
            results[ip] = (False, "Cancelled")
            dashboard.update(ip, status="Cancelled", done=True, ok=False)
            return

        success, msg = _process_device(
            ip=ip,
            build_dir=build_dir,
            custom_www=args.custom_www,
            www_only=args.www_only,
            timeout=args.timeout,
            dashboard=dashboard,
            cancel_event=cancel_event,
            abort_event=abort_event,
        )
        results[ip] = (success, msg)
        dashboard.update(ip, status=msg, done=True, ok=success)

    start_time = time.time()
    executor = ThreadPoolExecutor(max_workers=max_workers)
    futures = [executor.submit(worker, ip) for ip in ips]
    interrupted = False
    aborted = False
    has_hung_workers = False
    try:
        pending = set(futures)
        while pending:
            done, pending = wait(pending, timeout=0.2)
            for f in done:
                f.result()
    except KeyboardInterrupt:
        interrupted = True
        cancel_event.set()
        for ip, f in zip(ips, futures):
            if f.cancel():
                results[ip] = (False, "Cancelled")
                dashboard.update(ip, status="Cancelled", done=True, ok=False)
        pending = {f for f in futures if not f.done()}
        if pending:
            dashboard.set_footer(
                "Ctrl+C: pending uploads cancelled. Finishing running upload(s) — press Ctrl+C again to abort."
            )
            try:
                while pending:
                    done, pending = wait(pending, timeout=0.2)
                    for f in done:
                        if not f.cancelled():
                            f.result()
            except KeyboardInterrupt:
                aborted = True
                abort_event.set()
                dashboard.set_footer("Ctrl+C again: aborting running upload(s)...")
                _, still_running = wait(pending, timeout=0.5)
                for ip, f in zip(ips, futures):
                    if f in still_running and ip not in results:
                        results[ip] = (False, "Aborted")
                        dashboard.update(ip, status="Aborted", done=True, ok=False)
                has_hung_workers = bool(still_running)
                executor.shutdown(wait=False, cancel_futures=True)
            else:
                executor.shutdown(wait=True, cancel_futures=True)
        else:
            executor.shutdown(wait=True, cancel_futures=True)
        dashboard.set_footer(None)
    except BaseException:
        cancel_event.set()
        abort_event.set()
        executor.shutdown(wait=False, cancel_futures=True)
        raise
    else:
        executor.shutdown(wait=True)

    elapsed = time.time() - start_time
    succeeded = sum(1 for s, _ in results.values() if s)
    cancelled = [ip for ip in ips if ip in results and not results[ip][0] and results[ip][1].startswith("Cancelled")]
    aborted_ips = [ip for ip in ips if ip in results and not results[ip][0] and results[ip][1] == "Aborted"]
    failed = [
        (ip, results[ip][1])
        for ip in ips
        if ip in results
        and not results[ip][0]
        and not results[ip][1].startswith("Cancelled")
        and results[ip][1] != "Aborted"
    ]

    if aborted:
        print("\n[ABORTED] Interrupted twice by user. Running uploads aborted.", file=sys.stderr)
    elif interrupted:
        print(
            "\n[CANCELLED] Interrupted by user. Pending uploads cancelled; in-progress uploads finished.",
            file=sys.stderr,
        )

    print(f"\n{'=' * 50}")
    print(f"Upload Summary: {succeeded}/{len(results)} device(s) updated successfully in {elapsed:.1f}s")
    if aborted_ips:
        print(f"Aborted device(s) ({len(aborted_ips)}):")
        for ip in aborted_ips:
            print(f"  - {ip}")
    if cancelled:
        print(f"Cancelled device(s) ({len(cancelled)}):")
        for ip in cancelled:
            print(f"  - {ip}")
    if failed:
        print(f"Failed device(s) ({len(failed)}):")
        for ip, msg in failed:
            print(f"  - {ip}: {msg}")
    print(f"{'=' * 50}")

    if interrupted:
        if has_hung_workers:
            sys.stdout.flush()
            sys.stderr.flush()
            os._exit(130)
        sys.exit(130)
    sys.exit(0 if not failed else 2)


if __name__ == "__main__":
    main()
