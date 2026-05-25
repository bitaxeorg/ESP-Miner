# Cypher Gamma Max Predictive Efficiency Fork

Fork identity:

- `forkName`: Cypher Gamma Max
- `forkCodename`: CYPHER-GAMMA-MAX
- `creator`: 0xjc65eth
- `creatorTag`: CYPHER-0xJC65ETH

This fork targets Bitaxe devices that use the `BM1368` or `BM1370` ASIC, including
Supra, SupraHex, Gamma, GammaDuo, and GammaTurbo profiles.

The goal is not to predict winning Bitcoin nonces. SHA-256 mining has no exploitable
nonce prediction path without breaking Bitcoin's proof-of-work assumptions. Instead,
this fork measures operational nonce-search efficiency and recommends safer tuning
actions for supported BM13xx-based devices.

## First milestone

The firmware now computes a passive predictive-efficiency profile once per statistics
tick and exposes it through `/api/system/info` inside the `predictiveEfficiency`
object.

Tracked signals:

- `score`: composite efficiency/stability score from 0 to 100.
- `hashPerWatt`: one-minute hashrate divided by measured power.
- `usefulShareRatio`: accepted shares divided by total submitted shares.
- `bm1368Profile`: true when the active ASIC profile is BM1368.
- `bm1370Profile`: true when the active ASIC profile is BM1370.
- `profileName`: active tuning profile name.
- `forkName`: fork branding exposed by this firmware.
- `forkCodename`: short codename for identifying this firmware line.
- `creator`: creator identity for this fork.
- `creatorTag`: Cypher creator tag for API-level attribution.
- `gammaProfile`: compatibility alias for `bm1370Profile`.
- `thermalMarginC`: margin below the active profile's thermal ceiling used by this model.
- `hashrateRatio`: one-minute hashrate divided by expected hashrate.
- `errorPenalty`: normalized ASIC error pressure.
- `latencyPenalty`: normalized pool response latency pressure.
- `recommendedAction`: one of `hold`, `explore_up`, `reduce_clock`, `cool_down`, or `check_pool`.
- `reason`: short explanation for the recommendation.

## Safety boundary

Autotune is enabled by default in this fork through the `predictiveAutotune` NVS/API
setting. It only changes ASIC frequency, and only on BM1368/BM1370 profiles. It does not
change voltage, fan speed, pool settings, or job scheduling.

The agent waits for a warm-up window, then makes small 25 MHz trials. It rolls back
when the score drops, ASIC errors rise, rejected shares rise, or thermal margin is
exhausted. It also steps down on thermal pressure before the firmware's stronger
overheat protection is needed.

Additional exposed fields:

- `autotuneEnabled`: true when the autonomous tuner is allowed to act.
- `agentState`: one of `disabled`, `warmup`, `monitor`, `trial`, `rollback`, or `throttle`.
- `agentReason`: short explanation for the agent state.
- `tunedFrequency`: current requested ASIC frequency in MHz.
- `bestFrequency`: last accepted stable frequency in MHz.
- `trialFrequency`: active or most recent trial frequency in MHz.
- `baselineScore`: score used to evaluate the current trial.
- `lastTuneMs`: monotonic timestamp of the last agent frequency action.

## Suggested next milestone

Add AxeOS controls for `predictiveAutotune` and a dedicated Predictive Efficiency
panel so the agent can be paused, resumed, and inspected from the web UI.
