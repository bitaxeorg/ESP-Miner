#!/usr/bin/env python3
"""Validate GridPool Direct Milestone 1 work against a local Bitcoin node.

The probe fetches a real getblocktemplate, fetches GridPool payout outputs,
builds the same slot-0 coinbase/merkle/header artifacts as the firmware
builder, and asks Bitcoin Core to validate the assembled block with
getblocktemplate proposal mode.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
BECH32_CHARS = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
BECH32M_CONST = 0x2BC830A3
DEFAULT_TAG = b"/GridPool Direct/"


def sha256d(data: bytes) -> bytes:
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def encode_varint(value: int) -> bytes:
    if value < 0xFD:
        return bytes([value])
    if value <= 0xFFFF:
        return b"\xfd" + value.to_bytes(2, "little")
    if value <= 0xFFFFFFFF:
        return b"\xfe" + value.to_bytes(4, "little")
    return b"\xff" + value.to_bytes(8, "little")


def push_data(data: bytes) -> bytes:
    if len(data) > 75:
        raise ValueError("only direct pushes up to 75 bytes are supported")
    return bytes([len(data)]) + data


def bip34_height(height: int) -> bytes:
    value = height
    encoded = bytearray()
    while value:
        encoded.append(value & 0xFF)
        value >>= 8
    if not encoded:
        encoded.append(0)
    if encoded[-1] & 0x80:
        encoded.append(0)
    return push_data(bytes(encoded))


def base58_decode(text: str) -> bytes:
    value = 0
    for char in text:
        value *= 58
        try:
            value += BASE58_ALPHABET.index(char)
        except ValueError as exc:
            raise ValueError("invalid base58 character") from exc

    raw = value.to_bytes((value.bit_length() + 7) // 8, "big") if value else b""
    leading_zeroes = len(text) - len(text.lstrip("1"))
    return b"\x00" * leading_zeroes + raw


def hrp_expand(hrp: str) -> list[int]:
    return [ord(x) >> 5 for x in hrp] + [0] + [ord(x) & 31 for x in hrp]


def bech32_polymod(values: list[int]) -> int:
    generators = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    chk = 1
    for value in values:
        top = chk >> 25
        chk = (chk & 0x1FFFFFF) << 5 ^ value
        for i, generator in enumerate(generators):
            if (top >> i) & 1:
                chk ^= generator
    return chk


def bech32_decode(text: str) -> tuple[str, list[int], str]:
    if text.lower() != text and text.upper() != text:
        raise ValueError("mixed-case bech32 string")
    text = text.lower()
    if any(ord(x) < 33 or ord(x) > 126 for x in text):
        raise ValueError("invalid bech32 character range")
    sep = text.rfind("1")
    if sep < 1 or sep + 7 > len(text):
        raise ValueError("invalid bech32 separator")

    hrp = text[:sep]
    data = []
    for char in text[sep + 1 :]:
        index = BECH32_CHARS.find(char)
        if index < 0:
            raise ValueError("invalid bech32 data character")
        data.append(index)

    check = bech32_polymod(hrp_expand(hrp) + data)
    if check == 1:
        variant = "bech32"
    elif check == BECH32M_CONST:
        variant = "bech32m"
    else:
        raise ValueError("invalid bech32 checksum")
    return hrp, data[:-6], variant


def convertbits(data: list[int], from_bits: int, to_bits: int, pad: bool) -> bytes:
    acc = 0
    bits = 0
    ret = bytearray()
    maxv = (1 << to_bits) - 1
    max_acc = (1 << (from_bits + to_bits - 1)) - 1
    for value in data:
        if value < 0 or value >> from_bits:
            raise ValueError("invalid convertbits value")
        acc = ((acc << from_bits) | value) & max_acc
        bits += from_bits
        while bits >= to_bits:
            bits -= to_bits
            ret.append((acc >> bits) & maxv)
    if pad:
        if bits:
            ret.append((acc << (to_bits - bits)) & maxv)
    elif bits >= from_bits or ((acc << (to_bits - bits)) & maxv):
        raise ValueError("invalid convertbits padding")
    return bytes(ret)


def address_to_script(address: str) -> bytes:
    if address.lower().startswith(("bc1", "tb1", "bcrt1")):
        hrp, data, variant = bech32_decode(address)
        if hrp not in {"bc", "tb", "bcrt"} or not data:
            raise ValueError("unsupported bech32 HRP")
        version = data[0]
        program = convertbits(data[1:], 5, 8, False)
        if version > 16 or not (2 <= len(program) <= 40):
            raise ValueError("invalid witness program")
        if version == 0:
            if variant != "bech32" or len(program) not in {20, 32}:
                raise ValueError("invalid v0 witness program")
            return b"\x00" + bytes([len(program)]) + program
        if variant != "bech32m":
            raise ValueError("v1+ witness programs must use bech32m")
        return bytes([0x50 + version, len(program)]) + program

    decoded = base58_decode(address)
    if len(decoded) != 25 or sha256d(decoded[:-4])[:4] != decoded[-4:]:
        raise ValueError("invalid base58check address")
    version = decoded[0]
    payload = decoded[1:21]
    if version in (0x00, 0x6F):
        return b"\x76\xa9\x14" + payload + b"\x88\xac"
    if version in (0x05, 0xC4):
        return b"\xa9\x14" + payload + b"\x87"
    raise ValueError("unsupported base58 address version")


def serialize_coinbase(
    template: dict[str, Any],
    slot0_address: str,
    outputs: list[dict[str, Any]],
    extranonce: bytes,
    tag: bytes,
    include_witness: bool,
) -> tuple[bytes, int]:
    coinbase_value = int(template["coinbasevalue"])
    gridpool_total = sum(int(output["value"]) for output in outputs)
    if gridpool_total > coinbase_value:
        raise ValueError("GridPool outputs exceed coinbase value")

    scriptsig = bip34_height(int(template["height"])) + push_data(tag)
    if extranonce:
        scriptsig += push_data(extranonce)

    default_commitment = template.get("default_witness_commitment") or ""
    witness_script = bytes.fromhex(default_commitment) if default_commitment else b""
    output_count = 1 + len(outputs) + (1 if witness_script else 0)
    slot0_value = coinbase_value - gridpool_total

    tx = bytearray()
    tx += (2).to_bytes(4, "little")
    if include_witness:
        tx += b"\x00\x01"
    tx += encode_varint(1)
    tx += b"\x00" * 32
    tx += (0xFFFFFFFF).to_bytes(4, "little")
    tx += encode_varint(len(scriptsig)) + scriptsig
    tx += (0xFFFFFFFF).to_bytes(4, "little")
    tx += encode_varint(output_count)
    tx += slot0_value.to_bytes(8, "little")
    slot0_script = address_to_script(slot0_address)
    tx += encode_varint(len(slot0_script)) + slot0_script

    for output in outputs:
        script = address_to_script(str(output["address"]))
        tx += int(output["value"]).to_bytes(8, "little")
        tx += encode_varint(len(script)) + script

    if witness_script:
        tx += (0).to_bytes(8, "little")
        tx += encode_varint(len(witness_script)) + witness_script

    if include_witness:
        tx += encode_varint(1)
        tx += encode_varint(32)
        tx += b"\x00" * 32

    tx += (0).to_bytes(4, "little")
    return bytes(tx), slot0_value


def tx_weight(full_tx: bytes, legacy_tx: bytes) -> int:
    return len(legacy_tx) * 4 + (len(full_tx) - len(legacy_tx))


def display_hash_to_internal(hex_hash: str) -> bytes:
    raw = bytes.fromhex(hex_hash)
    if len(raw) != 32:
        raise ValueError("hash is not 32 bytes")
    return raw[::-1]


def hash_to_display(hash_bytes: bytes) -> str:
    return hash_bytes[::-1].hex()


def merkle_path_and_root(coinbase_txid: bytes, txids: list[bytes]) -> tuple[list[bytes], bytes]:
    level = [coinbase_txid] + txids
    index = 0
    path: list[bytes] = []
    while len(level) > 1:
        sibling = index ^ 1
        if sibling >= len(level):
            sibling = index
        path.append(level[sibling])

        next_level = []
        for i in range(0, len(level), 2):
            right = i + 1 if i + 1 < len(level) else i
            next_level.append(sha256d(level[i] + level[right]))
        level = next_level
        index //= 2
    return path, level[0]


def serialize_header(template: dict[str, Any], merkle_root: bytes, nonce: int = 0) -> bytes:
    header = bytearray()
    header += int(template["version"]).to_bytes(4, "little", signed=False)
    header += display_hash_to_internal(str(template["previousblockhash"]))
    header += merkle_root
    header += int(template["curtime"]).to_bytes(4, "little", signed=False)
    header += int(str(template["bits"]), 16).to_bytes(4, "little", signed=False)
    header += nonce.to_bytes(4, "little", signed=False)
    return bytes(header)


def read_conf(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";") or line.startswith("["):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip().lower()] = value.strip()
    return values


def rpc_url_from_conf(conf: dict[str, str]) -> str:
    port = conf.get("rpcport")
    if not port:
        rpcbind = conf.get("rpcbind", "")
        if ":" in rpcbind and rpcbind.rsplit(":", 1)[1].isdigit():
            port = rpcbind.rsplit(":", 1)[1]
    return f"http://127.0.0.1:{port or '8332'}"


def cookie_credentials(datadir: Path) -> tuple[str, str] | None:
    for path in (datadir / ".cookie", datadir / "mainnet" / ".cookie"):
        if not path.exists():
            continue
        text = path.read_text().strip()
        if ":" in text:
            user, password = text.split(":", 1)
            return user, password
    return None


class RpcClient:
    def __init__(self, url: str, user: str, password: str) -> None:
        self.url = url
        token = base64.b64encode(f"{user}:{password}".encode()).decode()
        self.auth_header = f"Basic {token}"
        self.next_id = 1

    def call(self, method: str, params: list[Any] | None = None) -> tuple[Any, int, float]:
        payload = {
            "jsonrpc": "1.0",
            "id": f"gridpool-m1-{self.next_id}",
            "method": method,
            "params": params or [],
        }
        self.next_id += 1
        body = json.dumps(payload).encode()
        request = urllib.request.Request(
            self.url,
            data=body,
            headers={
                "Authorization": self.auth_header,
                "Content-Type": "application/json",
                "User-Agent": "esp-miner-gridpool-m1-probe/1.0",
            },
        )
        start = time.perf_counter()
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                response_body = response.read()
        except urllib.error.HTTPError as exc:
            response_body = exc.read()
            raise RuntimeError(f"RPC {method} HTTP {exc.code}: {response_body.decode(errors='replace')}") from exc
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        decoded = json.loads(response_body)
        if decoded.get("error") is not None:
            raise RuntimeError(f"RPC {method} error: {decoded['error']}")
        return decoded.get("result"), len(response_body), elapsed_ms


def http_get_json(url: str) -> tuple[Any, int, float]:
    request = urllib.request.Request(url, headers={"User-Agent": "esp-miner-gridpool-m1-probe/1.0"})
    start = time.perf_counter()
    with urllib.request.urlopen(request, timeout=60) as response:
        body = response.read()
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    return json.loads(body), len(body), elapsed_ms


def compact_outputs(payouts: dict[str, Any]) -> list[dict[str, Any]]:
    raw_outputs = payouts.get("coinbaseOutputs")
    if not isinstance(raw_outputs, list):
        raise ValueError("GridPool payout response did not include coinbaseOutputs")
    outputs = []
    for output in raw_outputs:
        outputs.append({"value": int(output["value"]), "address": str(output["address"])})
    return outputs


def build_candidate(
    template: dict[str, Any],
    outputs: list[dict[str, Any]],
    slot0_address: str,
    extranonce: bytes,
    tag: bytes,
    trim_to_weight: bool,
) -> dict[str, Any]:
    full_coinbase, slot0_value = serialize_coinbase(template, slot0_address, outputs, extranonce, tag, True)
    legacy_coinbase, _ = serialize_coinbase(template, slot0_address, outputs, extranonce, tag, False)
    coinbase_txid = sha256d(legacy_coinbase)

    txs = list(template.get("transactions", []))
    selected_count = len(txs)
    coinbase_weight = tx_weight(full_coinbase, legacy_coinbase)
    weight_limit = int(template.get("weightlimit", 4_000_000))

    def estimated_weight(count: int) -> int:
        tx_count_len = len(encode_varint(count + 1))
        tx_weight_sum = sum(int(tx.get("weight", 0)) for tx in txs[:count])
        return 80 * 4 + tx_count_len * 4 + coinbase_weight + tx_weight_sum

    if trim_to_weight:
        while selected_count > 0 and estimated_weight(selected_count) > weight_limit:
            selected_count -= 1

    selected_txs = txs[:selected_count]
    txids = [display_hash_to_internal(str(tx["txid"])) for tx in selected_txs]
    path, merkle_root = merkle_path_and_root(coinbase_txid, txids)
    header = serialize_header(template, merkle_root)

    block = bytearray(header)
    block += encode_varint(1 + len(selected_txs))
    block += full_coinbase
    for tx in selected_txs:
        block += bytes.fromhex(str(tx["data"]))

    return {
        "block_hex": bytes(block).hex(),
        "header_hex": header.hex(),
        "coinbase_hex": full_coinbase.hex(),
        "coinbase_txid": hash_to_display(coinbase_txid),
        "merkle_root": hash_to_display(merkle_root),
        "merkle_path": [sibling.hex() for sibling in path],
        "slot0_value_sats": slot0_value,
        "coinbase_weight": coinbase_weight,
        "estimated_block_weight": estimated_weight(selected_count),
        "weight_limit": weight_limit,
        "selected_tx_count": selected_count,
        "dropped_tx_count": len(txs) - selected_count,
        "full_tx_count": len(txs),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gridpool-url", default="https://gridpool.net", help="GridPool base URL")
    parser.add_argument("--datadir", default="/home/keegreil/.bitcoin", help="Bitcoin Core datadir")
    parser.add_argument("--conf", default="/home/keegreil/.bitcoin/gridpool-template.conf", help="bitcoin.conf path")
    parser.add_argument("--rpc-url", default=None, help="Bitcoin Core JSON-RPC URL")
    parser.add_argument("--rpc-user", default=None, help="Bitcoin Core RPC username")
    parser.add_argument("--rpc-password", default=None, help="Bitcoin Core RPC password")
    parser.add_argument("--slot0-address", default=None, help="Slot-0 miner payout address")
    parser.add_argument("--extranonce-hex", default="475031", help="Coinbase extranonce hex")
    parser.add_argument("--tag", default=DEFAULT_TAG.decode(), help="Coinbase tag")
    parser.add_argument("--no-trim-to-weight", action="store_true", help="Do not drop tail transactions if estimate exceeds weightlimit")
    parser.add_argument("--json-out", default=None, help="Optional path for JSON measurement output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    datadir = Path(args.datadir).expanduser()
    conf_path = Path(args.conf).expanduser()
    conf = read_conf(conf_path)
    rpc_url = args.rpc_url or rpc_url_from_conf(conf)

    rpc_user = args.rpc_user or conf.get("rpcuser")
    rpc_password = args.rpc_password or conf.get("rpcpassword")
    if not (rpc_user and rpc_password):
        cookie = cookie_credentials(datadir)
        if cookie:
            rpc_user, rpc_password = cookie
    if not (rpc_user and rpc_password):
        raise SystemExit("RPC credentials not found in args, conf, or cookie")

    timings: dict[str, float] = {}
    sizes: dict[str, int] = {}
    rpc = RpcClient(rpc_url, rpc_user, rpc_password)

    template, gbt_response_bytes, timings["getblocktemplate_ms"] = rpc.call(
        "getblocktemplate", [{"rules": ["segwit"]}]
    )
    sizes["getblocktemplate_response_bytes"] = gbt_response_bytes
    sizes["template_json_bytes"] = len(json.dumps(template, separators=(",", ":")).encode())

    payouts_url = args.gridpool_url.rstrip("/") + "/api/mining/payouts"
    payouts, payout_bytes, timings["gridpool_payouts_ms"] = http_get_json(payouts_url)
    sizes["gridpool_payouts_response_bytes"] = payout_bytes
    outputs = compact_outputs(payouts)
    if not outputs and not args.slot0_address:
        raise SystemExit("slot0 address required when GridPool output list is empty")
    slot0_address = args.slot0_address or str(outputs[0]["address"])

    start = time.perf_counter()
    candidate = build_candidate(
        template,
        outputs,
        slot0_address,
        bytes.fromhex(args.extranonce_hex),
        args.tag.encode(),
        trim_to_weight=not args.no_trim_to_weight,
    )
    timings["build_candidate_ms"] = (time.perf_counter() - start) * 1000.0

    proposal_result, proposal_response_bytes, timings["proposal_ms"] = rpc.call(
        "getblocktemplate", [{"mode": "proposal", "data": candidate["block_hex"], "rules": ["segwit"]}]
    )
    sizes["proposal_response_bytes"] = proposal_response_bytes
    sizes["coinbase_tx_bytes"] = len(bytes.fromhex(candidate["coinbase_hex"]))
    sizes["block_proposal_bytes"] = len(bytes.fromhex(candidate["block_hex"]))
    sizes["txid_storage_bytes"] = candidate["selected_tx_count"] * 32
    sizes["merkle_working_level_bytes"] = (candidate["selected_tx_count"] + 1) * 32
    sizes["merkle_path_bytes"] = len(candidate["merkle_path"]) * 32

    report = {
        "gridpool_url": args.gridpool_url.rstrip("/"),
        "rpc_url": rpc_url,
        "template_height": int(template["height"]),
        "previous_block_hash": str(template["previousblockhash"]),
        "bits": str(template["bits"]),
        "coinbase_value_sats": int(template["coinbasevalue"]),
        "gridpool_sequence": payouts.get("sequence"),
        "slot0_address": slot0_address,
        "gridpool_output_count": len(outputs),
        "gridpool_output_total_sats": sum(int(output["value"]) for output in outputs),
        "proposal_result": proposal_result,
        "proposal_accepted": proposal_result is None,
        "timings": timings,
        "sizes": sizes,
        "candidate": {key: value for key, value in candidate.items() if key != "block_hex"},
    }

    output = json.dumps(report, indent=2, sort_keys=True)
    print(output)
    if args.json_out:
        Path(args.json_out).write_text(output + "\n")
    return 0 if proposal_result is None else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (urllib.error.URLError, ValueError, RuntimeError) as exc:
        print(f"gridpool_m1_probe.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
