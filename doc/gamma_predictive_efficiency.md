# Gamma Predictive Efficiency Fork

This fork targets the single-chip Bitaxe Gamma profile (`BM1370`, family `GAMMA`) first.

The goal is not to predict winning Bitcoin nonces. SHA-256 mining has no exploitable
nonce prediction path without breaking Bitcoin's proof-of-work assumptions. Instead,
this fork measures operational nonce-search efficiency and recommends safer tuning
actions for the Gamma.

## First milestone

The firmware now computes a passive predictive-efficiency profile once per statistics
tick and exposes it through `/api/system/info` inside the `predictiveEfficiency`
object.

Tracked signals:

- `score`: composite efficiency/stability score from 0 to 100.
- `hashPerWatt`: one-minute hashrate divided by measured power.
- `usefulShareRatio`: accepted shares divided by total submitted shares.
- `thermalMarginC`: margin below the Gamma thermal ceiling used by this model.
- `hashrateRatio`: one-minute hashrate divided by expected hashrate.
- `errorPenalty`: normalized ASIC error pressure.
- `latencyPenalty`: normalized pool response latency pressure.
- `recommendedAction`: one of `hold`, `explore_up`, `reduce_clock`, `cool_down`, or `check_pool`.
- `reason`: short explanation for the recommendation.

## Safety boundary

This milestone is intentionally passive. It does not change voltage, frequency, fan
speed, pool settings, or job scheduling. The next milestone should consume
`recommendedAction` behind an explicit NVS setting, with conservative Gamma-only
frequency steps and rollback on higher error rate, higher reject rate, or lower
hash-per-watt.

## Suggested next milestone

Add a Gamma-only autotune mode:

1. Require an opt-in config flag.
2. Run only after a warm-up period.
3. Try one small frequency step at a time.
4. Compare hash-per-watt and reject/error trends over a fixed window.
5. Persist the best stable setting only after repeated confirmation.
6. Roll back immediately on over-temperature, error spikes, or rejected-share spikes.
