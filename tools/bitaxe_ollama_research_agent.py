#!/usr/bin/env python3
"""
Autonomous Bitaxe tuning companion for AxeOS + Ollama.

Runs outside the miner on a LAN machine. It polls AxeOS telemetry, asks a local
Ollama model for a JSON decision, optionally includes web research snippets, and
applies accepted settings through the AxeOS API.
"""

import argparse
import html
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


SAFE_SETTING_KEYS = {
    "frequency",
    "coreVoltage",
    "autofanspeed",
    "manualFanSpeed",
    "minFanSpeed",
    "temptarget",
    "predictiveAutotune",
}

RISKY_SETTING_KEYS = {
    "stratumURL",
    "stratumPort",
    "stratumUser",
    "stratumPassword",
    "stratumSuggestedDifficulty",
    "stratumExtranonceSubscribe",
    "stratumTLS",
    "useFallbackStratum",
    "fallbackStratumURL",
    "fallbackStratumPort",
    "fallbackStratumUser",
    "fallbackStratumPassword",
    "fallbackStratumSuggestedDifficulty",
}


def http_json(url, method="GET", payload=None, timeout=20):
    body = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url, data=body, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        data = response.read().decode("utf-8")
        if not data:
            return {}
        return json.loads(data)


def http_text(url, timeout=20):
    request = urllib.request.Request(url, headers={"User-Agent": "bitaxe-ollama-research-agent/0.1"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        content_type = response.headers.get("content-type", "")
        if "text" not in content_type and "html" not in content_type and "json" not in content_type:
            return ""
        return response.read(120000).decode("utf-8", errors="replace")


def compact_text(text, max_chars=4000):
    text = re.sub(r"<script.*?</script>", " ", text, flags=re.I | re.S)
    text = re.sub(r"<style.*?</style>", " ", text, flags=re.I | re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    text = html.unescape(text)
    text = re.sub(r"\s+", " ", text).strip()
    return text[:max_chars]


def fetch_references(urls, timeout):
    references = []
    for url in urls:
        try:
            references.append({"url": url, "text": compact_text(http_text(url, timeout=timeout))})
        except Exception as exc:
            references.append({"url": url, "error": str(exc)})
    return references


def ollama_decision(ollama_url, model, telemetry, references, authority, timeout):
    prompt = {
        "role": "user",
        "content": (
            "You are a Bitaxe mining tuning agent. Return ONLY compact JSON with keys: "
            "settings object, actions array, reason string, confidence number 0..1. "
            "Allowed settings depend on authority. Never propose unsafe thermal operation. "
            "Prefer small changes. If data is weak, return empty settings.\n\n"
            f"Authority: {authority}\n"
            f"Telemetry JSON: {json.dumps(telemetry, separators=(',', ':'))}\n"
            f"References JSON: {json.dumps(references, separators=(',', ':'))}"
        ),
    }
    payload = {
        "model": model,
        "stream": False,
        "messages": [
            {
                "role": "system",
                "content": (
                    "You tune Bitcoin ASIC miner settings using telemetry. "
                    "Output valid JSON only. No prose. No markdown."
                ),
            },
            prompt,
        ],
        "options": {"temperature": 0.1},
    }
    response = http_json(f"{ollama_url.rstrip('/')}/api/chat", method="POST", payload=payload, timeout=timeout)
    content = response.get("message", {}).get("content", "").strip()
    match = re.search(r"\{.*\}", content, flags=re.S)
    if not match:
        raise ValueError(f"Ollama did not return JSON: {content[:200]}")
    return json.loads(match.group(0))


def clamp_decision(decision, allow_risky):
    settings = decision.get("settings", {})
    if not isinstance(settings, dict):
        settings = {}

    allowed = set(SAFE_SETTING_KEYS)
    if allow_risky:
        allowed.update(RISKY_SETTING_KEYS)

    cleaned = {}
    for key, value in settings.items():
        if key not in allowed:
            continue
        cleaned[key] = value

    # Hard clamps stay in place even with full authority.
    if "manualFanSpeed" in cleaned:
        cleaned["manualFanSpeed"] = int(max(0, min(100, cleaned["manualFanSpeed"])))
    if "minFanSpeed" in cleaned:
        cleaned["minFanSpeed"] = int(max(0, min(99, cleaned["minFanSpeed"])))
    if "temptarget" in cleaned:
        cleaned["temptarget"] = int(max(35, min(66, cleaned["temptarget"])))
    if "frequency" in cleaned:
        cleaned["frequency"] = float(max(300, min(700, cleaned["frequency"])))
    if "coreVoltage" in cleaned:
        cleaned["coreVoltage"] = int(max(900, min(1300, cleaned["coreVoltage"])))

    decision["settings"] = cleaned
    return decision


def apply_decision(bitaxe_url, decision, timeout):
    settings = decision.get("settings", {})
    if settings:
        http_json(f"{bitaxe_url.rstrip('/')}/api/system", method="PATCH", payload=settings, timeout=timeout)

    for action in decision.get("actions", []):
        if action == "pause":
            http_json(f"{bitaxe_url.rstrip('/')}/api/system/pause", method="POST", timeout=timeout)
        elif action == "resume":
            http_json(f"{bitaxe_url.rstrip('/')}/api/system/resume", method="POST", timeout=timeout)
        elif action == "restart":
            http_json(f"{bitaxe_url.rstrip('/')}/api/system/restart", method="POST", timeout=timeout)


def main():
    parser = argparse.ArgumentParser(description="Bitaxe AxeOS + Ollama autonomous research tuner")
    parser.add_argument("--bitaxe-url", required=True, help="Base URL, e.g. http://192.168.1.50")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    parser.add_argument("--model", default="llama3.1")
    parser.add_argument("--interval", type=int, default=300)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--research-url", action="append", default=[], help="Reference URL to include; repeatable")
    parser.add_argument("--apply", action="store_true", help="Apply settings to AxeOS. Without this, dry-run only.")
    parser.add_argument("--full-authority", action="store_true", help="Allow risky fields such as pool settings.")
    args = parser.parse_args()

    authority = "full" if args.full_authority else "safe"
    while True:
        try:
            telemetry = http_json(f"{args.bitaxe_url.rstrip('/')}/api/system/info", timeout=args.timeout)
            references = fetch_references(args.research_url, args.timeout)
            decision = ollama_decision(args.ollama_url, args.model, telemetry, references, authority, args.timeout)
            decision = clamp_decision(decision, args.full_authority)

            print(json.dumps({"decision": decision}, indent=2, sort_keys=True), flush=True)
            if args.apply:
                apply_decision(args.bitaxe_url, decision, args.timeout)
        except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            print(json.dumps({"error": str(exc)}), file=sys.stderr, flush=True)

        time.sleep(args.interval)


if __name__ == "__main__":
    main()
