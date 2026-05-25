# Ollama Research Agent

This fork includes an external autonomous companion agent:

```bash
python3 tools/bitaxe_ollama_research_agent.py \
  --bitaxe-url http://YOUR-BITAXE-IP \
  --ollama-url http://127.0.0.1:11434 \
  --model llama3.1 \
  --research-url https://github.com/bitaxeorg/ESP-Miner \
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

## Authority Modes

Default mode allows:

- `frequency`
- `coreVoltage`
- `autofanspeed`
- `manualFanSpeed`
- `minFanSpeed`
- `temptarget`
- `predictiveAutotune`

`--full-authority` additionally allows pool-related settings. Even then, hard
clamps remain in the script for frequency, voltage, fan speed, and temperature
target.

## Dry Run

Omit `--apply` to watch decisions without changing AxeOS:

```bash
python3 tools/bitaxe_ollama_research_agent.py \
  --bitaxe-url http://YOUR-BITAXE-IP \
  --model llama3.1
```

## Continuous Research

Pass one or more `--research-url` values. The agent fetches compact text snippets
from those URLs on each cycle and provides them to Ollama as references.

The first implementation intentionally uses explicit URLs instead of open-ended
web search. This prevents the agent from following low-quality or hostile content
without operator intent.
