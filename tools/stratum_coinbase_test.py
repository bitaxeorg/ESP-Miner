#!/usr/bin/env python3
"""Serve synthetic Stratum V1 jobs to a physical miner, validating submitted work."""

import argparse
import asyncio
import hashlib
import json
import logging
import math
import secrets
import struct
import time
from dataclasses import dataclass, field
from fractions import Fraction

LOG = logging.getLogger("coinbase-test")
DIFF_ONE_TARGET = 0xFFFF << 208
VERSION = 0x20000000
VERSION_MASK = 0x1FFFE000
NBITS = 0x170FFFFF
EXTRANONCE2_SIZE = 8


def sha256d(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def compact_size(value):
    if value < 0xFD:
        return bytes([value])
    if value <= 0xFFFF:
        return b"\xfd" + struct.pack("<H", value)
    return b"\xfe" + struct.pack("<I", value)


def coinbase_parts(count, extranonce1_size=4):
    # BIP34 height 900000, followed by a test tag and the two extranonces.
    script = b"\x03\xa0\xbb\x0d" + b"/ESP-Miner coinbase test/"
    prefix = (
        struct.pack("<I", 1) + b"\x01" + bytes(32) + b"\xff" * 4
        + compact_size(len(script) + extranonce1_size + EXTRANONCE2_SIZE) + script
    )
    suffix = bytearray(b"\xff" * 4 + compact_size(count))
    value, remainder = divmod(312_500_000, count)
    for index in range(count):
        key = hashlib.sha256(b"ESP-Miner payout" + struct.pack("<I", index)).digest()
        suffix += struct.pack("<Q", value + (index < remainder)) + b"\x22\x51\x20" + key
    suffix += bytes(4)
    return prefix, bytes(suffix)


@dataclass
class Job:
    job_id: str
    count: int
    prefix: bytes
    suffix: bytes
    ntime: int
    accepted: int = 0
    rejected: int = 0
    retired_at: float | None = None
    shares: set = field(default_factory=set)

    def notify(self):
        return {
            "id": None, "method": "mining.notify",
            "params": [self.job_id, "00" * 32, self.prefix.hex(), self.suffix.hex(),
                       [], f"{VERSION:08x}", f"{NBITS:08x}", f"{self.ntime:08x}", True],
        }

    def header(self, extranonce1, extranonce2, ntime, nonce, version_bits):
        merkle_root = sha256d(self.prefix + extranonce1 + extranonce2 + self.suffix)
        return struct.pack("<I", VERSION | version_bits) + bytes(32) + merkle_root + struct.pack("<III", ntime, NBITS, nonce)


@dataclass
class Config:
    counts: tuple = (110, 1800, 10000, 110)
    interval: float = 60
    difficulty: Fraction = Fraction(128)
    chunk_size: int = 0
    chunk_delay: float = 0.001

    @property
    def target(self):
        return min((1 << 256) - 1, DIFF_ONE_TARGET * self.difficulty.denominator // self.difficulty.numerator)


class Session:
    def __init__(self, reader, writer, config):
        self.reader, self.writer, self.config = reader, writer, config
        self.peer = str(writer.get_extra_info("peername"))
        self.extranonce1 = secrets.token_bytes(4)
        self.version_mask = 0
        self.subscribed = False
        self.authorized = False
        self.jobs = {}
        self.current = None
        self.sequence = 0
        self.send_lock = asyncio.Lock()
        self.job_task = None

    async def send(self, message):
        data = (json.dumps(message, separators=(",", ":")) + "\n").encode()
        async with self.send_lock:
            chunk = self.config.chunk_size or len(data)
            for offset in range(0, len(data), chunk):
                self.writer.write(data[offset:offset + chunk])
                await self.writer.drain()
                if self.config.chunk_size and offset + chunk < len(data):
                    await asyncio.sleep(self.config.chunk_delay)
        return len(data)

    async def reply(self, request_id, result, error=None):
        await self.send({"id": request_id, "result": result, "error": error})

    def summary(self):
        if self.current:
            job = self.current
            LOG.info("%s STAGE job=%s outputs=%d accepted=%d rejected=%d %s",
                     self.peer, job.job_id, job.count, job.accepted, job.rejected,
                     "PASS" if job.accepted else "NO_SHARES")

    async def publish_job(self, count):
        self.summary()
        if self.current:
            self.current.retired_at = time.monotonic()
        self.sequence += 1
        prefix, suffix = coinbase_parts(count, len(self.extranonce1))
        job = Job(f"{self.sequence:08x}", count, prefix, suffix, int(time.time()))
        self.jobs[job.job_id] = job
        while len(self.jobs) > 2:
            del self.jobs[next(iter(self.jobs))]
        self.current = job
        line_size = await self.send(job.notify())
        LOG.info("%s JOB job=%s outputs=%d coinb2=%d bytes line=%d bytes difficulty=%s",
                 self.peer, job.job_id, count, len(suffix), line_size, float(self.config.difficulty))

    async def send_jobs(self):
        while True:
            for count in self.config.counts:
                await self.publish_job(count)
                await asyncio.sleep(self.config.interval)

    def check_share(self, params):
        if not self.authorized or not isinstance(params, list) or len(params) not in (5, 6):
            return None, [20, "Invalid share parameters or unauthorized", None]
        job = self.jobs.get(params[1]) if isinstance(params[1], str) else None
        if not job or (job.retired_at is not None and time.monotonic() - job.retired_at > 5):
            return job, [21, "Stale or unknown job", None]
        try:
            extranonce2 = bytes.fromhex(params[2])
            fields = params[3:]
            if len(extranonce2) != EXTRANONCE2_SIZE or any(
                not isinstance(value, str) or len(value) != 8
                or any(c not in "0123456789abcdefABCDEF" for c in value) for value in fields
            ):
                raise ValueError("Invalid hex fields")
            ntime, nonce = int(fields[0], 16), int(fields[1], 16)
            bits = int(fields[2], 16) if len(fields) == 3 else 0
            if bits & ~self.version_mask or (self.version_mask and len(params) != 6):
                raise ValueError("Invalid version bits")
            if not job.ntime <= ntime <= job.ntime + 7200:
                raise ValueError("Invalid ntime")
        except (ValueError, TypeError):
            return job, [20, "Invalid extranonce, time, nonce or version bits", None]
        key = (extranonce2, ntime, nonce, bits)
        if key in job.shares:
            return job, [22, "Duplicate share", None]
        header = job.header(self.extranonce1, extranonce2, ntime, nonce, bits)
        if int.from_bytes(sha256d(header), "little") > self.config.target:
            return job, [23, "Share does not meet test difficulty", None]
        job.shares.add(key)
        return job, None

    async def handle(self, request):
        request_id, method = request.get("id"), request.get("method")
        params = request.get("params", [])
        if method == "mining.configure":
            result = {}
            if not isinstance(params, list) or len(params) != 2 or not isinstance(params[0], list) or not isinstance(params[1], dict):
                await self.reply(request_id, False, [20, "Invalid configure parameters", None])
                return
            for extension in params[0]:
                result[str(extension)] = False
            if "version-rolling" in params[0]:
                try:
                    mask = int(params[1].get("version-rolling.mask", "ffffffff"), 16) & VERSION_MASK
                    minimum = int(params[1].get("version-rolling.min-bit-count", 0))
                    if mask.bit_count() >= minimum:
                        self.version_mask = mask
                        result.update({"version-rolling": True, "version-rolling.mask": f"{mask:08x}"})
                except (TypeError, ValueError):
                    pass
            await self.reply(request_id, result)
        elif method == "mining.subscribe":
            self.subscribed = True
            await self.reply(request_id, [[["mining.notify", self.extranonce1.hex()]], self.extranonce1.hex(), EXTRANONCE2_SIZE])
        elif method == "mining.authorize":
            if not self.subscribed:
                await self.reply(request_id, False, [25, "Subscribe first", None])
                return
            self.authorized = True
            await self.reply(request_id, True)
            if self.job_task is None:
                await self.send({"id": None, "method": "mining.set_difficulty", "params": [float(self.config.difficulty)]})
                self.job_task = asyncio.create_task(self.send_jobs())
        elif method in ("mining.suggest_difficulty", "mining.extranonce.subscribe"):
            await self.reply(request_id, True)
        elif method == "mining.submit":
            job, error = self.check_share(params)
            if job:
                if error:
                    job.rejected += 1
                else:
                    job.accepted += 1
            LOG.info("%s %s job=%s outputs=%s %s", self.peer, "REJECT" if error else "ACCEPT",
                     job.job_id if job else "unknown", job.count if job else "unknown", error[1] if error else "verified full coinbase")
            await self.reply(request_id, error is None, error)
        else:
            await self.reply(request_id, False, [20, "Unsupported method", None])

    async def run(self):
        LOG.info("%s CONNECT", self.peer)
        try:
            while line := await self.reader.readline():
                try:
                    request = json.loads(line)
                    if not isinstance(request, dict):
                        raise ValueError("Request must be an object")
                except ValueError:
                    await self.reply(None, False, [20, "Invalid JSON request", None])
                    continue
                await self.handle(request)
        except (ConnectionError, OSError, ValueError) as error:
            LOG.info("%s connection ended: %s", self.peer, error)
        finally:
            if self.job_task:
                self.job_task.cancel()
                try:
                    await self.job_task
                except (asyncio.CancelledError, ConnectionError, OSError):
                    pass
            self.summary()
            LOG.info("%s DISCONNECT", self.peer)
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except (ConnectionError, OSError):
                pass


async def serve(args, config):
    server = await asyncio.start_server(lambda r, w: Session(r, w, config).run(), args.host, args.port, limit=8192)
    LOG.info("Listening on %s:%d; synthetic work only, no Bitcoin payouts", args.host, args.port)
    LOG.info("Sequence=%s interval=%ss difficulty=%s; pool TLS must be disabled", config.counts, config.interval, float(config.difficulty))
    async with server:
        await server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="Listen address (default: all IPv4 interfaces)")
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument("--counts", default="110,1800,10000,110", help="Repeating sequence of payout counts")
    parser.add_argument("--interval", type=float, default=60, help="Seconds to mine each job")
    parser.add_argument("--difficulty", default="128", help="Fixed share difficulty; miner suggestions are ignored")
    parser.add_argument("--chunk-size", type=int, default=0, help="Limit each socket write to this many bytes (0: whole message)")
    parser.add_argument("--chunk-delay", type=float, default=0.001, help="Seconds between chunk writes")
    args = parser.parse_args()
    try:
        counts = tuple(int(count) for count in args.counts.split(","))
        difficulty = Fraction(args.difficulty)
        if not counts or any(not 1 <= count <= 100000 for count in counts):
            raise ValueError("Payout counts must be between 1 and 100000")
        if not Fraction(1, 10000) <= difficulty <= 4294967295:
            raise ValueError("Difficulty must be between 0.0001 and 4294967295")
        if not math.isfinite(args.interval) or args.interval <= 0 or not 1 <= args.port <= 65535:
            raise ValueError("Interval must be positive and port must be between 1 and 65535")
        if args.chunk_size < 0 or not math.isfinite(args.chunk_delay) or args.chunk_delay < 0:
            raise ValueError("Chunk size and delay must be nonnegative")
    except (ValueError, ZeroDivisionError) as error:
        parser.error(str(error))
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    config = Config(counts, args.interval, difficulty, args.chunk_size, args.chunk_delay)
    try:
        asyncio.run(serve(args, config))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
