#!/usr/bin/env python3
"""
Stratum V1 Protocol Test Tool with client.reconnect Trigger

This tool models the Stratum V1 protocol as used for SHA256-based coins (e.g., Bitcoin).
It simulates a minimalist pool that demonstrates core protocol behavior,
and now includes a "reconnect storm" after 5 job notifications.
"""

import asyncio
import json
import time
import os
from datetime import datetime

LOG_FILE = "stratum_log.txt"
issued_jobs = set()
session_counter = 0

def log(msg):
    timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S]")
    line = f"{timestamp} {msg}"
    print(line)
    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(line + "\n")

def print_handshake_diagram():
    diagram = [
        "        Miner Side                              Pool Side",
        "        ----------                              ----------",
        "            |                                        |",
        "            |    — TCP connection established —>     |",
        "            |                                        |",
        "            |    ——— mining.subscribe ————————>      |",
        "            |                                        |",
        "            |   <—— subscription response ————       |",
        "            |     [includes extranonce1, etc.]       |",
        "            |                                        |",
        "            |    ——— mining.authorize ————————>      |",
        "            |                                        |",
        "            |   <—— authorize result (True) ———      |",
        "            |                                        |",
        "            |    <—— mining.set_difficulty ———       |",
        "            |   <—— mining.notify ————————           |",
        "            |     [initial job assignment]           |",
        "            |                                        |",
        "        ...miner now starts hashing...               |",
        ""
    ]
    for line in diagram:
        log(line)

def generate_fake_job(job_id):
    issued_jobs.add(job_id.lower())
    return {
        "id": None,
        "method": "mining.notify",
        "params": [
            job_id,
            "00" * 32,
            "0200000001000000000000000000000000000000000000000000000000000000",
            "0000000000000000",
            ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"],
            "20000202",
            "1d00ffff",
            format(int(time.time()), '08x'),
            True
        ]
    }

async def handle_client(reader, writer):
    global session_counter
    peer = writer.get_extra_info("peername")
    log(f"Client connected: {peer}")
    job_task = None
    job_id_counter = 1

    session_index = session_counter
    session_counter += 1

    if session_index % 2 == 0:
        pool_id = "mrr"
        extranonce1 = "8000845a"
        extranonce2_size = 4
        version_mask = "1fffe000"
        job_interval = 10
        log("Simulating MiningRigRentals (MRR) persona")
    else:
        pool_id = "nicehash"
        extranonce1 = "deadbeefcafebabe"
        extranonce2_size = 8
        version_mask = "ffffffff"
        job_interval = 3
        log("Simulating Nicehash persona")

    job_sent = 0
    set_extranonce_sent = False

    async def send_msg(msg):
        try:
            payload = json.dumps(msg) + "\r\n"
            writer.write(payload.encode())
            await writer.drain()
            log(f">>> Sent: {payload.strip()}")
        except Exception as e:
            log(f"Send Error: {e}")

    async def send_periodic_jobs():
        nonlocal job_id_counter, job_sent, extranonce1
        try:
            while True:
                await asyncio.sleep(job_interval)
                job_id = format(job_id_counter, "04x")
                job_id_counter += 1
                job_sent += 1

                if pool_id == "nicehash" and job_sent % 4 == 0:
                    extranonce1 = ''.join(format(x, '02x') for x in os.urandom(8))
                    await send_msg({
                        "id": None,
                        "method": "mining.set_extranonce",
                        "params": [extranonce1, extranonce2_size]
                    })

                await send_msg(generate_fake_job(job_id))

                if job_sent == 5:
                    log("Triggering client.reconnect after 5 jobs.")
                    await send_msg({
                        "id": None,
                        "method": "client.reconnect",
                        "params": ["stratum.example.com", 3333, 5]
                    })
                    log("Closing connection after reconnect directive...")
                    await asyncio.sleep(1)
                    break
        except asyncio.CancelledError:
            log("Periodic job cancelled")

    try:
        while not reader.at_eof():
            data = await reader.readline()
            if not data:
                break
            decoded = data.decode().strip()
            log(f"<<< Raw data: {decoded}")
            try:
                msg = json.loads(decoded)
                log(f"<<< Parsed JSON: {msg}")
            except json.JSONDecodeError:
                log(f"Invalid JSON: {decoded}")
                continue

            method = msg.get("method")
            msg_id = msg.get("id")
            params = msg.get("params", [])

            if method == "mining.configure":
                await send_msg({
                    "id": msg_id,
                    "result": [params[0], {"version-rolling.mask": version_mask}],
                    "error": None
                })

            elif method == "mining.subscribe":
                await send_msg({
                    "id": msg_id,
                    "result": [
                        [["mining.set_difficulty", "300.000"], ["mining.notify", "sub-id"]],
                        extranonce1,
                        extranonce2_size
                    ],
                    "error": None
                })

            elif method == "mining.authorize":
                await send_msg({
                    "id": msg_id,
                    "result": True,
                    "error": None
                })
                await asyncio.sleep(0.1)
                await send_msg({
                    "id": None,
                    "method": "mining.set_difficulty",
                    "params": [300]
                })

            elif method == "mining.extranonce.subscribe":
                log("Client subscribed to extranonce updates.")
                await send_msg({
                    "id": msg_id,
                    "result": True,
                    "error": None
                })
                if pool_id == "nicehash" and not set_extranonce_sent:
                    await send_msg({
                        "id": None,
                        "method": "mining.set_extranonce",
                        "params": [extranonce1, extranonce2_size]
                    })
                    set_extranonce_sent = True
                await asyncio.sleep(0.2)
                await send_msg(generate_fake_job(format(job_id_counter, "04x")))
                job_id_counter += 1
                job_task = asyncio.create_task(send_periodic_jobs())

            elif method == "mining.suggest_difficulty":
                log("Received suggest_difficulty (optional). No response needed.")

            elif method == "mining.submit":
                log(f"Share submitted: {params}")
                if len(params) >= 2:
                    job_id = str(params[1]).lower()
                    if job_id in issued_jobs:
                        log(f"Accepted share for job {job_id}")
                        await send_msg({
                            "id": msg_id,
                            "result": True,
                            "error": None
                        })
                    else:
                        log(f"Rejected share for unknown job {job_id}")
                        await send_msg({
                            "id": msg_id,
                            "result": None,
                            "error": [21, "Unknown job ID", None]
                        })
                else:
                    log("Invalid submit: missing job ID")
                    await send_msg({
                        "id": msg_id,
                        "result": None,
                        "error": [20, "Invalid submit format", None]
                    })

            elif method == "client.reconnect":
                log("Client requested reconnect.")
                if len(params) >= 1:
                    host = params[0]
                    port = params[1] if len(params) > 1 else 3333
                    delay = params[2] if len(params) > 2 else 0
                    log(f"Client requested reconnect to {host}:{port} after {delay}s")
                if msg_id is not None:
                    await send_msg({
                        "id": msg_id,
                        "result": True,
                        "error": None
                    })
                break

            else:
                log(f"Unhandled method: {method}")

    except Exception as e:
        log(f"Connection error: {e}")
    finally:
        if job_task:
            job_task.cancel()
            try:
                await job_task
            except:
                pass
        writer.close()
        try:
            await writer.wait_closed()
        except Exception as e:
            log(f"wait_closed error: {e}")
        log(f"Client disconnected: {peer}")

async def main():
    if os.path.exists(LOG_FILE):
        os.remove(LOG_FILE)

    print(__doc__)
    log("Stratum V1 Protocol Test Tool (with reconnect)")
    log("Simulating a minimalist pool with proper handshake and reconnect simulation.")
    log("")
    print_handshake_diagram()

    server = await asyncio.start_server(handle_client, "0.0.0.0", 3333)
    log(f"Stratum mock pool listening on {server.sockets[0].getsockname()}")
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    asyncio.run(main())

