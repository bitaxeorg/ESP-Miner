#!/usr/bin/env python3
"""
Autonomous Bitaxe tuning companion for AxeOS + Ollama.

Runs outside the miner on a LAN machine. It polls AxeOS telemetry, asks a local
Ollama model for a JSON decision, optionally includes web research snippets, and
applies accepted settings through the AxeOS API.
"""

import argparse
import datetime as dt
import html
import json
import re
import sqlite3
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

SEARCH_URL = "https://html.duckduckgo.com/html/"


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


def init_db(path):
    conn = sqlite3.connect(path)
    conn.execute(
        """
        create table if not exists telemetry (
            id integer primary key autoincrement,
            ts text not null,
            payload text not null,
            score real,
            hash_per_watt real,
            frequency real,
            temp real,
            error_percentage real,
            shares_accepted integer,
            shares_rejected integer
        )
        """
    )
    conn.execute(
        """
        create table if not exists decisions (
            id integer primary key autoincrement,
            ts text not null,
            applied integer not null,
            payload text not null
        )
        """
    )
    conn.execute(
        """
        create table if not exists research (
            id integer primary key autoincrement,
            ts text not null,
            query text,
            url text,
            title text,
            snippet text
        )
        """
    )
    conn.commit()
    return conn


def telemetry_float(telemetry, key, default=0.0):
    value = telemetry.get(key, default)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def save_telemetry(conn, telemetry):
    predictive = telemetry.get("predictiveEfficiency", {}) or {}
    conn.execute(
        """
        insert into telemetry (
            ts, payload, score, hash_per_watt, frequency, temp, error_percentage,
            shares_accepted, shares_rejected
        ) values (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            dt.datetime.utcnow().isoformat(timespec="seconds") + "Z",
            json.dumps(telemetry, separators=(",", ":")),
            telemetry_float(predictive, "score"),
            telemetry_float(predictive, "hashPerWatt"),
            telemetry_float(telemetry, "frequency"),
            telemetry_float(telemetry, "temp"),
            telemetry_float(telemetry, "errorPercentage"),
            int(telemetry.get("sharesAccepted", 0) or 0),
            int(telemetry.get("sharesRejected", 0) or 0),
        ),
    )
    conn.commit()


def recent_summary(conn, limit=24):
    rows = conn.execute(
        """
        select score, hash_per_watt, frequency, temp, error_percentage,
               shares_accepted, shares_rejected
        from telemetry order by id desc limit ?
        """,
        (limit,),
    ).fetchall()
    if not rows:
        return {}

    def avg(index):
        values = [r[index] for r in rows if r[index] is not None]
        return sum(values) / len(values) if values else 0.0

    latest = rows[0]
    return {
        "samples": len(rows),
        "latest": {
            "score": latest[0],
            "hashPerWatt": latest[1],
            "frequency": latest[2],
            "temp": latest[3],
            "errorPercentage": latest[4],
            "sharesAccepted": latest[5],
            "sharesRejected": latest[6],
        },
        "averages": {
            "score": avg(0),
            "hashPerWatt": avg(1),
            "frequency": avg(2),
            "temp": avg(3),
            "errorPercentage": avg(4),
        },
    }


def save_decision(conn, decision, applied):
    conn.execute(
        "insert into decisions (ts, applied, payload) values (?, ?, ?)",
        (
            dt.datetime.utcnow().isoformat(timespec="seconds") + "Z",
            1 if applied else 0,
            json.dumps(decision, separators=(",", ":")),
        ),
    )
    conn.commit()


def save_research(conn, query, items):
    ts = dt.datetime.utcnow().isoformat(timespec="seconds") + "Z"
    for item in items:
        conn.execute(
            "insert into research (ts, query, url, title, snippet) values (?, ?, ?, ?, ?)",
            (ts, query, item.get("url"), item.get("title"), item.get("snippet")),
        )
    conn.commit()


def recent_research(conn, limit=10):
    rows = conn.execute(
        "select query, url, title, snippet from research order by id desc limit ?",
        (limit,),
    ).fetchall()
    return [
        {"query": row[0], "url": row[1], "title": row[2], "snippet": row[3]}
        for row in rows
    ]


def default_search_queries(telemetry):
    profile = (telemetry.get("predictiveEfficiency") or {}).get("profileName") or telemetry.get("ASICModel") or "Bitaxe"
    rejected = telemetry.get("sharesRejected", 0) or 0
    error = telemetry_float(telemetry, "errorPercentage")
    temp = telemetry_float(telemetry, "temp")
    queries = [
        f"Bitaxe {profile} BM1368 BM1370 optimal frequency voltage efficiency",
        f"ESP-Miner {profile} ASIC error percentage tuning",
    ]
    if rejected:
        queries.append("Bitaxe ESP-Miner rejected shares stale pool latency tuning")
    if error > 2:
        queries.append(f"Bitaxe {profile} high ASIC errors frequency voltage")
    if temp > 70:
        queries.append(f"Bitaxe {profile} thermal throttling cooling fan tuning")
    return queries


def duckduckgo_search(query, timeout=20, max_results=5):
    url = SEARCH_URL + "?" + urllib.parse.urlencode({"q": query})
    text = http_text(url, timeout=timeout)
    results = []
    for match in re.finditer(
        r'class="result__a" href="(?P<href>[^"]+)".*?>(?P<title>.*?)</a>.*?class="result__snippet".*?>(?P<snippet>.*?)</a>',
        text,
        flags=re.S,
    ):
        href = html.unescape(match.group("href"))
        parsed = urllib.parse.urlparse(href)
        params = urllib.parse.parse_qs(parsed.query)
        if "uddg" in params:
            href = params["uddg"][0]
        results.append(
            {
                "query": query,
                "url": href,
                "title": compact_text(match.group("title"), 240),
                "snippet": compact_text(match.group("snippet"), 600),
            }
        )
        if len(results) >= max_results:
            break
    return results


def fetch_references(urls, timeout):
    references = []
    for url in urls:
        try:
            references.append({"url": url, "text": compact_text(http_text(url, timeout=timeout))})
        except Exception as exc:
            references.append({"url": url, "error": str(exc)})
    return references


def ollama_decision(ollama_url, model, telemetry, references, memory, authority, timeout):
    prompt = {
        "role": "user",
        "content": (
            "You are a Bitaxe mining tuning agent. Return ONLY compact JSON with keys: "
            "settings object, actions array, reason string, confidence number 0..1. "
            "You may include experiment object with hypothesis, expected_gain, rollback_condition. "
            "Allowed settings depend on authority. Never propose unsafe thermal operation. "
            "Prefer measured experiments over big jumps. If data is weak, return empty settings.\n\n"
            f"Authority: {authority}\n"
            f"Telemetry JSON: {json.dumps(telemetry, separators=(',', ':'))}\n"
            f"Memory JSON: {json.dumps(memory, separators=(',', ':'))}\n"
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


def clamp_decision(decision, authority):
    settings = decision.get("settings", {})
    if not isinstance(settings, dict):
        settings = {}

    allowed = set(SAFE_SETTING_KEYS)
    if authority in {"full", "experimental"}:
        allowed.update(RISKY_SETTING_KEYS)

    cleaned = {}
    for key, value in settings.items():
        if key not in allowed:
            continue
        cleaned[key] = value

    # Hard clamps stay in place even with experimental authority.
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

    if authority != "experimental":
        for action in list(decision.get("actions", [])):
            if action == "restart":
                decision["actions"].remove(action)

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
    parser.add_argument("--db", default="bitaxe_agent.sqlite3")
    parser.add_argument("--research-url", action="append", default=[], help="Reference URL to include; repeatable")
    parser.add_argument("--research-query", action="append", default=[], help="Search query to run; repeatable")
    parser.add_argument("--auto-research", action="store_true", help="Generate telemetry-driven web searches every cycle.")
    parser.add_argument("--research-every", type=int, default=3, help="Run web research every N cycles.")
    parser.add_argument("--max-search-results", type=int, default=5)
    parser.add_argument("--apply", action="store_true", help="Apply settings to AxeOS. Without this, dry-run only.")
    parser.add_argument("--full-authority", action="store_true", help="Allow risky fields such as pool settings.")
    parser.add_argument("--authority", choices=["safe", "full", "experimental"], default=None)
    args = parser.parse_args()

    authority = args.authority or ("full" if args.full_authority else "safe")
    conn = init_db(args.db)
    cycle = 0
    while True:
        try:
            cycle += 1
            telemetry = http_json(f"{args.bitaxe_url.rstrip('/')}/api/system/info", timeout=args.timeout)
            save_telemetry(conn, telemetry)

            references = fetch_references(args.research_url, args.timeout)
            research_due = cycle == 1 or (args.research_every > 0 and cycle % args.research_every == 0)
            if research_due:
                queries = list(args.research_query)
                if args.auto_research:
                    queries.extend(default_search_queries(telemetry))
                for query in queries:
                    items = duckduckgo_search(query, timeout=args.timeout, max_results=args.max_search_results)
                    save_research(conn, query, items)

            references.extend(recent_research(conn))
            memory = recent_summary(conn)
            decision = ollama_decision(args.ollama_url, args.model, telemetry, references, memory, authority, args.timeout)
            decision = clamp_decision(decision, authority)

            print(json.dumps({"authority": authority, "memory": memory, "decision": decision}, indent=2, sort_keys=True), flush=True)
            if args.apply:
                apply_decision(args.bitaxe_url, decision, args.timeout)
            save_decision(conn, decision, args.apply)
        except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            print(json.dumps({"error": str(exc)}), file=sys.stderr, flush=True)

        time.sleep(args.interval)


if __name__ == "__main__":
    main()
