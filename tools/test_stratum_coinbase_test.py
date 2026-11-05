import asyncio
import hashlib
import json
import struct
import unittest
from fractions import Fraction

from stratum_coinbase_test import Config, Job, Session, coinbase_parts


def double_hash(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


class CoinbaseTests(unittest.TestCase):
    def test_serialized_transactions_have_distinct_payouts_and_correct_value(self):
        for count in (1, 110, 1800, 10000, 20000):
            with self.subTest(count=count):
                prefix, suffix = coinbase_parts(count)
                tx = prefix + bytes(4 + 8) + suffix
                self.assertEqual(tx[:5], b"\x01\x00\x00\x00\x01")
                self.assertEqual(tx[5:37], bytes(32))
                self.assertEqual(tx[37:41], b"\xff" * 4)
                self.assertEqual(tx[42:46], b"\x03\xa0\xbb\x0d")
                pos = 42 + tx[41]
                self.assertEqual(tx[pos:pos + 4], b"\xff" * 4)
                pos += 4
                if tx[pos] < 253:
                    actual_count, pos = tx[pos], pos + 1
                else:
                    actual_count = struct.unpack_from("<H", tx, pos + 1)[0]
                    pos += 3
                self.assertEqual(actual_count, count)
                scripts, value = set(), 0
                for _ in range(count):
                    value += struct.unpack_from("<Q", tx, pos)[0]
                    self.assertEqual(tx[pos + 8:pos + 11], b"\x22\x51\x20")
                    scripts.add(tx[pos + 9:pos + 43])
                    pos += 43
                self.assertEqual(len(scripts), count)
                self.assertEqual(value, 312_500_000)
                self.assertEqual(tx[pos:], bytes(4))
                if count == 10000:
                    self.assertEqual(len(suffix), 430011)
                if count == 20000:
                    self.assertGreater(len(suffix) * 2, 2 * 512 * 1024 + 8192)

    def test_header_matches_independent_node_crypto_reference(self):
        job = Job("ref", 1, b"\x01", b"\x03", 0x64495522)
        header = job.header(bytes.fromhex("01020304"), bytes(8), job.ntime, 0x12345678, 0x2000)
        self.assertEqual(len(header), 80)
        self.assertEqual(header[36:68].hex(), "9bfc4a13d1c9cc7c18b27223b2d4292aa88a39a29c16a2e0aac04a1e2fccbc7d")
        self.assertEqual(double_hash(header).hex(), "0487b365e6b08fa8da1912cac52491b2f195693115f8dad3cfe461b716cc03fa")


class PoolTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.sessions, self.handlers = [], []
        self.config = Config(counts=(110,), interval=60, difficulty=Fraction(1, 100_000_000), chunk_size=733, chunk_delay=0)

        async def connected(reader, writer):
            self.handlers.append(asyncio.current_task())
            session = Session(reader, writer, self.config)
            self.sessions.append(session)
            await session.run()

        self.server = await asyncio.start_server(connected, "127.0.0.1", 0, limit=8192)
        port = self.server.sockets[0].getsockname()[1]
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", port, limit=2 * 1024 * 1024)

    async def asyncTearDown(self):
        self.writer.close()
        await self.writer.wait_closed()
        self.server.close()
        await self.server.wait_closed()
        await asyncio.wait_for(asyncio.gather(*self.handlers), 5)

    async def receive(self):
        line = await asyncio.wait_for(self.reader.readline(), 5)
        return json.loads(line)

    async def request(self, request_id, method, params):
        self.writer.write((json.dumps({"id": request_id, "method": method, "params": params}) + "\n").encode())
        await self.writer.drain()

    async def handshake(self):
        await self.request(1, "mining.configure", [["version-rolling"], {"version-rolling.mask": "ffffffff"}])
        await self.request(2, "mining.subscribe", ["bitaxe/test"])
        await self.request(3, "mining.authorize", ["test.worker", "x"])
        configured, subscribed, authorized = await self.receive(), await self.receive(), await self.receive()
        self.assertEqual(configured["result"]["version-rolling.mask"], "1fffe000")
        self.assertEqual(subscribed["result"][2], 8)
        self.assertTrue(authorized["result"])
        self.extranonce1 = subscribed["result"][1]
        difficulty = await self.receive()
        self.assertEqual(difficulty["method"], "mining.set_difficulty")
        return await self.receive()

    def share(self, notify, want_valid=True, version_bits=0x2000):
        params = notify["params"]
        extranonce2 = "0000000000000000"
        coinbase = bytes.fromhex(params[2] + self.extranonce1 + extranonce2 + params[3])
        prefix = struct.pack("<I", int(params[5], 16) | version_bits) + bytes(32) + double_hash(coinbase)
        prefix += struct.pack("<II", int(params[7], 16), int(params[6], 16))
        for nonce in range(10000):
            valid = int.from_bytes(double_hash(prefix + struct.pack("<I", nonce)), "little") <= self.config.target
            if valid == want_valid:
                return ["test.worker", params[0], extranonce2, params[7], f"{nonce:08x}", f"{version_bits:08x}"]
        self.fail("Could not find a test nonce")

    async def test_real_tcp_large_jobs_validate_shares_and_return_to_small_jobs(self):
        notify = await self.handshake()
        for request_id, count in enumerate((110, 10000, 110), 10):
            if request_id != 10:
                await self.sessions[0].publish_job(count)
                notify = await self.receive()
            self.assertEqual(notify["method"], "mining.notify")
            await self.request(request_id, "mining.submit", self.share(notify))
            reply = await self.receive()
            self.assertEqual(reply, {"id": request_id, "result": True, "error": None})
            self.assertEqual(self.sessions[0].current.count, count)
            self.assertEqual(self.sessions[0].current.accepted, 1)
        self.assertEqual(len(self.sessions[0].jobs), 2)

    async def test_rejects_duplicate_low_difficulty_and_invalid_version_shares(self):
        notify = await self.handshake()
        params = self.share(notify)
        await self.request(4, "mining.submit", params)
        self.assertTrue((await self.receive())["result"])
        await self.request(5, "mining.submit", params)
        self.assertEqual((await self.receive())["error"][0], 22)
        await self.request(6, "mining.submit", self.share(notify, want_valid=False))
        self.assertEqual((await self.receive())["error"][0], 23)
        params[-1] = "80000000"
        await self.request(7, "mining.submit", params)
        self.assertEqual((await self.receive())["error"][0], 20)
        params[1] = "missing"
        await self.request(8, "mining.submit", params)
        self.assertEqual((await self.receive())["error"][0], 21)

    async def test_miner_difficulty_suggestion_does_not_change_test_difficulty(self):
        await self.handshake()
        await self.request(4, "mining.suggest_difficulty", [65536])
        self.assertTrue((await self.receive())["result"])
        self.assertEqual(self.config.difficulty, Fraction(1, 100_000_000))

    async def test_disconnect_during_oversized_job_can_reconnect_and_mine(self):
        self.config.counts = (110, 20000, 110)
        self.config.interval = 0.01
        self.config.chunk_delay = 0.001
        await self.handshake()

        async def large_job_started():
            while self.sessions[0].current.count != 20000:
                await asyncio.sleep(0.001)

        await asyncio.wait_for(large_job_started(), 5)
        self.writer.close()
        await self.writer.wait_closed()
        await asyncio.wait_for(self.handlers[0], 5)
        self.config.interval = 60
        self.config.chunk_delay = 0
        port = self.server.sockets[0].getsockname()[1]
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", port, limit=2 * 1024 * 1024)
        notify = await self.handshake()
        self.assertEqual(notify["params"][0], "00000001")
        await self.request(4, "mining.submit", self.share(notify))
        self.assertTrue((await self.receive())["result"])
        self.assertEqual(self.sessions[1].current.count, 110)


if __name__ == "__main__":
    unittest.main()
