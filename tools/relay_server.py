# WebSocket relay + HTTP frontend on a single port
# pip3 install aiohttp

import asyncio
import json
import sys
from datetime import datetime
from pathlib import Path

from aiohttp import web

sys.stdout.reconfigure(line_buffering=True)

PORT = 8888
CONTROL_HTML = Path(__file__).parent / "control.html"

gateway_ws = None
control_clients = set()
latest_status = None


def log(msg):
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


async def safe_send(ws, data):
    try:
        await ws.send_str(data)
    except Exception:
        pass


# --- HTTP handler ---
async def handle_index(request):
    return web.FileResponse(CONTROL_HTML)


# --- WebSocket: gateway ---
async def handle_gateway(request):
    global gateway_ws, latest_status

    ws = web.WebSocketResponse()
    await ws.prepare(request)

    if gateway_ws is not None and not gateway_ws.closed:
        await ws.close(code=1008, message=b"Another gateway already connected")
        return ws

    gateway_ws = ws
    log("GATEWAY: Connected")

    gw_status = json.dumps({"type": "gateway_status", "connected": True})
    for c in list(control_clients):
        await safe_send(c, gw_status)

    try:
        async for msg in ws:
            if msg.type == web.WSMsgType.TEXT:
                try:
                    message = json.loads(msg.data)
                except json.JSONDecodeError:
                    continue

                msg_type = message.get("type", "?")
                if msg_type == "status":
                    latest_status = message
                    miners = message.get("miners", [])
                    log(f"GATEWAY: Status — {len(miners)} miner(s)")
                    for m in miners:
                        log(f"  {m.get('hostname','?'):20s} {m.get('ip','?'):15s} "
                            f"{m.get('hashrate',0):8.1f} GH/s  {m.get('temp',0):.1f}C")
                else:
                    log(f"GATEWAY MSG: {msg_type} -> {json.dumps(message)[:300]}")

                if control_clients:
                    payload = json.dumps(message)
                    await asyncio.gather(*[safe_send(c, payload) for c in list(control_clients)])

            elif msg.type in (web.WSMsgType.ERROR, web.WSMsgType.CLOSE):
                break
    except Exception as e:
        log(f"GATEWAY: {e}")
    finally:
        gateway_ws = None
        latest_status = None
        log("GATEWAY: Disconnected")
        gw_status = json.dumps({"type": "gateway_status", "connected": False})
        for c in list(control_clients):
            await safe_send(c, gw_status)

    return ws


# --- WebSocket: control (frontend) ---
async def handle_control(request):
    global gateway_ws

    ws = web.WebSocketResponse()
    await ws.prepare(request)

    control_clients.add(ws)
    log(f"CONTROL: Connected (total: {len(control_clients)})")

    await safe_send(ws, json.dumps({
        "type": "gateway_status", "connected": gateway_ws is not None and not gateway_ws.closed
    }))

    if latest_status is not None:
        await safe_send(ws, json.dumps(latest_status))

    try:
        async for msg in ws:
            if msg.type == web.WSMsgType.TEXT:
                try:
                    message = json.loads(msg.data)
                except json.JSONDecodeError:
                    continue

                msg_type = message.get("type", "?")

                if gateway_ws is not None and not gateway_ws.closed:
                    try:
                        await gateway_ws.send_str(json.dumps(message))
                        if msg_type == "command":
                            log(f"COMMAND: {message.get('action','?')} -> {message.get('target','?')}")
                        else:
                            log(f"FORWARD: {msg_type}")
                    except Exception as e:
                        await safe_send(ws, json.dumps({
                            "type": "error", "message": str(e),
                        }))
                else:
                    log(f"FORWARD: {msg_type} (NO GATEWAY)")
                    await safe_send(ws, json.dumps({
                        "type": "error", "message": "No gateway connected",
                    }))

            elif msg.type in (web.WSMsgType.ERROR, web.WSMsgType.CLOSE):
                break
    except Exception:
        pass
    finally:
        control_clients.discard(ws)
        log(f"CONTROL: Disconnected (total: {len(control_clients)})")

    return ws


app = web.Application()
app.router.add_get("/", handle_index)
app.router.add_get("/gateway", handle_gateway)
app.router.add_get("/control", handle_control)

if __name__ == "__main__":
    log(f"Relay server on port {PORT}")
    log(f"  Frontend:  http://localhost:{PORT}/")
    log(f"  Gateway:   ws://localhost:{PORT}/gateway")
    log(f"  Control:   ws://localhost:{PORT}/control")
    log("")
    web.run_app(app, host="0.0.0.0", port=PORT, print=None)
