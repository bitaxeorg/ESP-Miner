# BM1370 Predictive Efficiency Fork

This fork targets Bitaxe devices that use the `BM1370` ASIC, including Gamma,
GammaDuo, and GammaTurbo profiles.

The goal is not to predict winning Bitcoin nonces. SHA-256 mining has no exploitable
nonce prediction path without breaking Bitcoin's proof-of-work assumptions. Instead,
this fork measures operational nonce-search efficiency and recommends safer tuning
actions for BM1370-based devices.

## First milestone

The firmware now computes a passive predictive-efficiency profile once per statistics
tick and exposes it through `/api/system/info` inside the `predictiveEfficiency`
object.

Tracked signals:

- `score`: composite efficiency/stability score from 0 to 100.
- `hashPerWatt`: one-minute hashrate divided by measured power.
- `usefulShareRatio`: accepted shares divided by total submitted shares.
- `bm1370Profile`: true when the active ASIC profile is BM1370.
- `gammaProfile`: compatibility alias for `bm1370Profile`.
- `thermalMarginC`: margin below the BM1370 thermal ceiling used by this model.
- `hashrateRatio`: one-minute hashrate divided by expected hashrate.
- `errorPenalty`: normalized ASIC error pressure.
- `latencyPenalty`: normalized pool response latency pressure.
- `recommendedAction`: one of `hold`, `explore_up`, `reduce_clock`, `cool_down`, or `check_pool`.
- `reason`: short explanation for the recommendation.

## Safety boundary

This milestone is intentionally passive. It does not change voltage, frequency, fan
speed, pool settings, or job scheduling. The next milestone should consume
`recommendedAction` behind an explicit NVS setting, with conservative BM1370-only
frequency steps and rollback on higher error rate, higher reject rate, or lower
hash-per-watt.

## Suggested next milestone

Add a BM1370-only autotune mode:

1. Require an opt-in config flag.
2. Run only after a warm-up period.
3. Try one small frequency step at a time.
4. Compare hash-per-watt and reject/error trends over a fixed window.
5. Persist the best stable setting only after repeated confirmation.
6. Roll back immediately on over-temperature, error spikes, or rejected-share spikes.
