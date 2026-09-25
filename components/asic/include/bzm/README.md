# BZM header boundaries

This directory contains the public interfaces used outside the ASIC component:

- `driver.h`: job submission, results, temperature and hashrate counters.
- `board.h`: startup, shutdown, frequency control and board safety callbacks.
- `bridge.h`, `power.h`, `frequency.h`, `lease_guard.h`, `runtime_health.h`
  and `telemetry.h`: interfaces used by Bonanza power and cooling code.
- `chain.h`: ASIC count shared by these interfaces.

Public headers include only other public headers. Board snapshots expose the
values needed by their callers; they do not expose transport, parser or job
storage state.

Implementation headers live in [private_include/bzm](../../private_include/bzm).
That directory contains startup operations, wire encoders and decoders, UART
transport, the parser, work storage, scheduling, topology and register details.
It is added through `PRIV_INCLUDE_DIRS` and is not exported to other components.

Unit tests add the private directory to their own private include path. They
can test the production implementations and use hardware fakes without adding
test functions to the public interface. The test build also compiles each
public header separately, with no private headers or test stubs available.
