# Ollama Research Agent

This fork includes an external autonomous companion agent:

```bash
python3 tools/bitaxe_ollama_research_agent.py \
  --bitaxe-url http://YOUR-BITAXE-IP \
  --ollama-url http://127.0.0.1:11434 \
  --model llama3.1 \
  --research-url https://github.com/bitaxeorg/ESP-Miner \
  --auto-research \
  --apply
```

The agent runs on a LAN computer, not on the ESP32. It polls AxeOS at
`/api/system/info`, calls Ollama's `/api/chat` endpoint with `stream:false`, and
applies JSON decisions through `PATCH /api/system`.

## Why it runs outside the miner

The ESP32 should not constantly scrape the internet or trust arbitrary internet
content to configure voltage, pools, or runtime behavior. The external agent gives
you stronger compute, local model control, logs, and easier shutdown while the
firmware still enforces hard thermal/electrical safety behavior.

## Cortex Mode

The agent now keeps a local SQLite memory database with telemetry, decisions, and
research snippets. It can generate telemetry-driven searches, feed the results to
Ollama, and treat each tuning move as a measured experiment.

```bash
python3 tools/bitaxe_ollama_research_agent.py \
  --bitaxe-url http://YOUR-BITAXE-IP \
  --model llama3.1 \
  --db bitaxe-cortex.sqlite3 \
  --auto-research \
  --research-every 2 \
  --authority experimental \
  --apply
```

## Authority Modes

Default mode allows:

- `frequency`
- `coreVoltage`
- `autofanspeed`
- `manualFanSpeed`
- `minFanSpeed`
- `temptarget`
- `predictiveAutotune`

`--authority full` additionally allows pool-related settings.

`--authority experimental` also permits restart actions. Even then, hard clamps
remain in the script for frequency, voltage, fan speed, and temperature target.
This mode is intended for supervised test rigs.

## Dry Run

Omit `--apply` to watch decisions without changing AxeOS:

```bash
python3 tools/bitaxe_ollama_research_agent.py \
  --bitaxe-url http://YOUR-BITAXE-IP \
  --model llama3.1
```

## Continuous Research

Pass one or more `--research-url` values. The agent fetches compact text snippets
from those URLs and provides them to Ollama as references.

Use `--auto-research` for generated web searches based on live telemetry:

```bash
python3 tools/bitaxe_ollama_research_agent.py \
  --bitaxe-url http://YOUR-BITAXE-IP \
  --model llama3.1 \
  --auto-research \
  --research-query "Bitaxe BM1368 undervolt efficiency tuning" \
  --apply
```

The agent stores recent research and sends a compact memory summary to Ollama,
including score, hash-per-watt, frequency, temperature, ASIC error rate, and share
counts. This gives the model continuity between cycles instead of making isolated
decisions.
