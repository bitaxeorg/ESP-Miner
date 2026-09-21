# BZM and Bonanza driver port onto PR #1972

Base: upstream PR #1972, `492392e677d7d208d85f8898ad3aad00f79d9e94`.
Driver source: the local Bonanza power integration at
`474c8bcb` (`merge/bzm-power-master-20260918`).

The series separates the BZM ASIC driver and Bonanza board power/safety.
It retains PR #1972's `asic_job_t`, `task_result`, public ASIC entry points,
global-state layout, and existing power/reset/regulator
signatures. ASIC and family identities are appended to the existing enums;
board 1002 uses the existing device configuration fields.

BZM's generated work handles, retained assignments, sequence barriers,
deduplication, telemetry and runtime safety checks remain driver-specific. Results resolve
rolled time into their owned job snapshot before returning `task_result`.
The existing job producer uses four software midstates and the negotiated
version mask; a zero mask remains zero for BZM. Invalidated work cancels driver
backpressure retries. Clean pool jobs and changes to the active pool difficulty
retire BZM's retained work and start a fast engine replacement rotation.
Bitmain packet encoders, drivers, and serial transport remain unchanged.

Bonanza owns its ordered hardware startup, bridge lease, fan interlocks, verified
safe-off, cooling and live clock/rail transitions. Its TPS546 binding
is deliberately separate from the upstream TPS546 configuration structure.
This duplicates regulator implementation code to avoid widening that shared
interface. UART buffering is likewise isolated in `bzm_serial`.
Startup and resume prime a fast first engine rotation. The board monitor owns
fan RPM sampling; the generic fan task reuses that measurement so duplicate
500 ms bridge tach requests cannot delay mining lease renewal and dispatch.

Production timing, filter and safety limits are fixed constants in the owning
implementation files, preserving the original production defaults. The board
and driver share only the telemetry freshness limit. No BZM/Bonanza Kconfig
settings are added; local nonce difficulty derives from the ASIC result filter.

Board 1002 requires the existing ESP-IDF USB console selection because its
RP2040 bridge uses GPIO43/44, the default UART console pins. For a fresh
Bonanza configuration, build with:

```sh
idf.py -B .cache/build-bonanza -DSDKCONFIG=.cache/sdkconfig.bonanza \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;configs/sdkconfig.bonanza" build
```

The overlay only selects existing ESP-IDF console options; it adds no custom
Kconfig settings. USB need not be connected. A default UART-console image
rejects Bonanza bridge initialization before enabling power.

Existing pause, resume, firmware OTA and restart routes delegate to the board
owner on Bonanza. Self-test waits for board startup and delegates shutdown.
Bonanza uses the existing headless display mode; external display support is
outside this port. This series does not import the fork's HTTP schema, health
dashboard, new bridge-update HTTP route, generalized driver framework, or asynchronous
Stratum submission subsystem. Bridge flashing, its update API, SWD transport,
and blank-bridge recovery are excluded; normal bridge control remains.
Unused health-reporting APIs, submission-context and generic event wrappers,
legacy initialization, frequency qualification, the superseded cooling helper,
and unused regulator profiles are also excluded. Task and regulator helpers
used only within their implementation stay private.

## Port validation before production cleanup

- ESP-IDF 6.0.2 application build passes, with 33% application partition free.
  The unchanged AxeOS bundle was reused from the PR #1972 baseline worktree.
- The complete QEMU suite reports **400 tests, 0 failures**. This includes
  existing Bitmain characterization and imported BZM protocol, parser,
  topology, work-store, deduplication, power-policy and safety tests.
  Added checks cover owned upstream result snapshots, timestamp overflow,
  and zero-mask/four-midstate job production.
- `git diff --check` passes. The common job implementation and headers,
  `asic.h`, `asic_common.h`, `global_state.h`, and public power, reset,
  initialization, thermal and regulator headers match the base.
- Local build and QEMU logs are under `.cache/driver-port/`.

### HTTP hardware validation, 2026-09-19

The clean firmware source `8f979df2c49be395207ad15ff3b6fac4f8b02692`
was built and installed through the existing HTTP OTA route on `bonanza.local`
(board 1002, four BZM ASICs, protocol-1.0 RP2040 bridge). No USB or SSH was used.
The tested application identifies as `v2.15.2rc0-40-g8f979df2`; its SHA-256 is
`e30aca538611596c23ca08a6c1c4e9d592b65cfc2ed564ec22cd0713da8596b4`.

The existing SV1 pool and saved 1200 MHz / 3000 mV settings were used. Startup
reached 800 MHz at 37 seconds, completed its first engine rotation at 45
seconds, then ramped to the saved 1200 MHz target.
At 490 seconds it reported 39 accepted shares, no rejected shares, 1.51 TH/s
(one-minute estimate), 60.3 C ASIC temperature, 60 C regulator temperature,
and 47.5 W. All four ASICs reported positive hashrate.

HTTP pause reached verified safe-off: reported frequency, core voltage and
power were zero, and accepted shares remained unchanged during a 20-second
pause. Resume restored 1200 MHz and produced 28 fresh accepted shares with no
rejects before the restart check, including a pool difficulty change from
1000 to 8192. The upstream pool reconnect resets session share counters;
these are new shares in the resumed session.

HTTP restart booted the same image from `ota_0`, returned to 1200 MHz, and
produced 21 accepted shares with no rejects by 236 seconds. The final sample
reported 1.48 TH/s, 60.7 C ASIC temperature, 60 C regulator temperature and
47.5 W, with positive hashrate on all four ASICs. This session observed pool
difficulty changes through 1000, 4096 and 8192. The board was left mining.

Configuration fingerprints matched the pre-deployment snapshot;
pool credentials, Wi-Fi, clock, voltage and fan settings were preserved.
The original `bzm-pm-424e8c3ad032` image remains available in `ota_1`, while
the tested image runs in `ota_0`.

The immutable image, manifest, filtered hardware logs and credential-free
HTTP samples are under `.cache/driver-port/hardware-validation/8f979df2/`.
SV2, pool fallback, explicit self-test, arbitrary live tuning and a long soak
remain untested on hardware. This branch retains upstream's synchronous pool
submission path; the fork's later nonblocking network-write behavior is
outside this driver-only port.

## Production cleanup

The cleanup is limited to the BZM/Bonanza additions after PR #1972. Shared
mining, result submission, Bitmain drivers, device/global-state layouts, HTTP
routes and generic power interfaces are unchanged from the reviewed PR head.

Startup directly performs verified shutdown, one bridge lease arm, rail enable,
chain discovery, sensor and PLL configuration, balanced engine activation, and
result reporting. Any failure closes dispatch and attempts every shutdown
output, then checks rail discharge and bridge state. The Bonanza power policy
continues to own pause, pool loss, cooling, OTA and restart exclusion. The host
watchdog and RP2040 lease remain active.

The selectable validation stages, duplicate supervisor, nonce-proof lifecycle,
parser clean-window/recovery qualification, RX-loss statistics polling, discard
byte traces and obsolete batch-dispatch/chain-discovery APIs are removed.
Startup ordering and useful register/address/error details remain explicit
in the hardware operations. Bridge status fields remain compatible with the
existing RP2040 protocol.

Balanced activation still uses the same synthetic startup load, pair order,
busy/config acknowledgements and UART barriers. All regulator configuration
writes and values are unchanged; startup readback retains power topology,
feedback/calibration and hard protection settings while dropping the optional
alert-routing, margin and timing audit. Live PGOOD, voltage, temperature, fan,
ASIC telemetry and trip checks remain mandatory.

Mining validates finite nonce difficulty, preserves job/sequence ownership and
deduplication, and counts real hashrate. Tuning waits for every engine to receive
work after startup or a PLL change without requiring a lucky nonce. Clean jobs
restart a full fast refresh; newer jobs count toward an in-progress clock
replacement so frequent pool notifications cannot starve tuning.

The regulator uses the existing Bonanza board profile directly. The unused
generic configuration path, voltage-ratio updates and duplicate profile
structure are removed. Periodic power snapshots read eight live registers;
startup still verifies the hard protection settings, and detailed status
decoding runs when a fault is reported. Unconsumed parser counters are removed;
discovery retains its discard and invalid-NOOP accounting.

ASIC startup now has one public entry point with one driver lock and rollback
path. Sensor settings and the 800 MHz startup PLL configuration are constants;
the configurable profiles, intermediate stage flags and progress counters are
removed. The activation loop owns pair ordering, so its helper retains error
details without duplicate completion state or a second completion gate. UART
draining, busy/config readback, fresh telemetry, cancellation and lease checks
remain in the production sequence.

Bonanza work generation, result decoding and flushing now always use four
midstates. The unused single-midstate mode, variable FIFO counts, legacy grid
lookup APIs and unused reactor states are removed. This pass changes no shared
modules and reduces ASIC support from 5,341 to 4,822 nonblank, noncomment C/header
lines using the same scope as the earlier BM13xx comparison.

The replacement tests compile the actual board, driver and regulator implementations
with hardware/timing fakes. They cover startup rollback, cancellation, shutdown
failures, maintenance discharge checks, watchdog expiry, transport errors,
finite difficulty, clean-job refresh and voltage/clock ordering. Protocol,
parser resynchronization, topology, real bring-up, job ownership and upstream
mining regression tests remain in the QEMU suite. Regulator tests also cover
failure at every profile write, unchanged protection limits during voltage
changes, live-read failures and fault reporting across the enable grace period.
The startup regression captures the pre-refactor register/delay/pair sequence
(trace `9f6d452e`, 1,089 events) and verifies the 100 ms and 30 ms receive windows.
New failure tests exercise each startup phase and the driver's reset, dispatch,
lock-release and error-reporting behavior. Tests of removed configuration and
staged APIs are replaced by the complete production-sequence checks.

Validation of the cleanup: ESP-IDF 6.0.2 firmware build passes with 34% of the
application partition free; QEMU reports **334 tests, 0 failures, 0 ignored**;
`git diff --check` passes. The only edited files predating PR #1972 are the two
ASIC CMake lists. Comparing the regulator code before and after profile-field
renaming and dead-branch removal confirms unchanged configuration writes,
runtime voltage writes, protection readback and fault classification.
The final build and QEMU logs are `.cache/review/simplify-*.log`; the binary
and manifest are in `.cache/review/simplify-artifacts/`.

### Cleanup hardware validation, 2026-09-19

Clean source `96e3a5d80dc0af85d6c95b1b4a4ccb73dec3d3dc` was rebuilt with
ESP-IDF 6.0.2 and installed on `bonanza.local` through HTTP OTA. The tested
firmware identifies as `v2.15.2rc0-41-g96e3a5d8`; its binary SHA-256 is
`c5743a1d44e528b868907cb9033093d963b6b9594712d90eaf8beac5e8b34085`.
The validation-note amendment changes no firmware source.

The existing SV1 pool and saved 1200 MHz / 3000 mV settings were preserved.
Startup reached 800 MHz at 35 seconds, completed the first engine rotation at
44 seconds and reached 1200 MHz by the 85-second sample. Verified samples were:

| Phase | Uptime | Accepted / rejected shares | One-minute hashrate | Power | ASIC / regulator temperature |
| --- | ---: | ---: | ---: | ---: | ---: |
| Startup | 333 s | 23 / 0 | 1.49 TH/s | 47.8 W | 62.9 / 62 C |
| Resume | 617 s | 15 / 0 | 1.52 TH/s | 47.8 W | 62.4 / 61 C |
| Restart | 222 s | 21 / 0 | 1.44 TH/s | 47.8 W | 62.9 / 62 C |

All four ASICs reported positive hashrate and zero ASIC errors in each verified
mining phase. Each phase produced fresh accepted shares; upstream pool
reconnection resets session counters. HTTP pause held verified safe-off for
30 seconds with zero frequency, core voltage and power, and no new shares.
Resume and restart both restored the saved target without intervention.

Current-boot hardware logs contain no errors or runtime regulator faults.
One restart warning reports an SMBus alert while the regulator is commanded
off; its `0x0840` status contains only OFF and not-in-regulation bits. Startup
clears faults before enabling the rail. This startup diagnostic predates the
cleanup; job-wait warnings during shutdown/startup were also reviewed.

Configuration fingerprints matched before and after every operation. The
tested image remains mining in `ota_1`, with the previous `8f979df2` image
retained in `ota_0` for rollback. The immutable firmware, manifest, filtered
logs, samples and summary are under
`.cache/review/hardware-validation/96e3a5d8/`.
SV2, pool fallback, explicit self-test, arbitrary tuning targets and a long
soak were not exercised in this hardware run.

## Public and private headers

BZM's public headers are under `components/asic/include/bzm/`. `driver.h`
contains the shared mining interface; `board.h` contains the controls and
snapshots needed by Bonanza power management. Public headers do not expose the
parser, transport, work store or startup operation callbacks.

Implementation headers are under `components/asic/private_include/bzm/`.
Only the ASIC component and its unit tests add that directory to their private
include paths. Tests still exercise the production implementations with the
same hardware fakes. Separate compile checks include each of the nine public
headers without private headers or test stubs.

This change preserves the C implementations and all 119 BZM header constants.
ESP-IDF 6.0.2 application and test builds pass; all 334 QEMU tests pass with no
failures or ignored tests. Build and interface-check evidence is under
`.cache/review/header-interfaces/`. No new hardware deployment was needed for
this header-only change.
