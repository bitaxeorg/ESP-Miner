# Web Authentication

Optional password protection for the AxeOS web interface. It stops anyone on the
same Wi-Fi/LAN from reading data or changing settings/firmware without credentials.

## How it works

- **HTTP Basic auth.** The browser shows its native sign-in dialog and re-sends
  the credentials to every same-origin request (API + WebSockets), so no custom
  login flow is needed.
- **Opt-in.** With no password set the gate is a no-op — behaviour is identical
  to before, so updating never locks anyone out.
- **Single choke point.** `is_network_allowed()` in `main/http_server/http_server.c`
  calls `http_auth_gate()`; the static-file handler calls it too so the login
  dialog appears on the first page load. When enabled, every `/api/*` route,
  both WebSockets, and OTA are covered.
- **Recovery.** AP setup mode (hold `BOOT`) bypasses auth, and the setup page shows
  the **Security** panel so a forgotten password can always be cleared there
  (the normal Settings pages are redirected to the setup page in AP mode). Erasing NVS also clears it
  (a full factory-image flash or `idf.py erase-flash`); a plain app-only re-flash
  keeps the stored hash because it lives in the separate `nvs` partition.
- **Storage.** Only a salted SHA-256 hash is kept, in its own NVS namespace
  (`authcfg`); it is never returned by any API.

Scope: plain HTTP on the LAN — protects against other users on the network, not
an on-path or physical/RF attacker. See the "Web Authentication" section of the
root `readme.md` for the user-facing guide and API examples.

## Code layout

| Area | Files |
|------|-------|
| Credential logic + storage (own component) | `components/http_auth/` |
| C unit tests (pure logic) | `components/http_auth/test/test_http_auth.c` |
| Server gate + `GET`/`POST /api/system/auth` | `main/http_server/http_server.c` |
| API spec | `main/http_server/openapi.yaml` |
| Settings → Security panel | `main/http_server/axe-os/src/app/components/security/` |
| Auth API client | `main/http_server/axe-os/src/app/services/auth.service.ts` |

## Reproducing the tests

### Prerequisites (once)

- **Node.js 22+** for the frontend.
- **ESP-IDF v5.5.x** + **qemu-xtensa** for the firmware/C tests:
  ```bash
  git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/v5.5.3/esp-idf
  cd ~/esp/v5.5.3/esp-idf
  ./install.sh esp32s3
  python tools/idf_tools.py install qemu-xtensa
  ```
  > Gotcha: `install.sh` refuses to run if your shell's `python3` is itself a
  > virtualenv/managed Python (`sys.prefix != sys.base_prefix`). Run it with the
  > system Python in front, e.g. `env PATH="/usr/bin:$PATH" ./install.sh esp32s3`.

Source the environment in each shell (again, system Python first if yours is a venv):
```bash
env PATH="/usr/bin:$PATH" bash -lc '. ~/esp/v5.5.3/esp-idf/export.sh; idf.py --version'
```

### Frontend unit tests (Angular / Karma)

```bash
cd main/http_server/axe-os
npm ci
CHROME_BIN=$(command -v google-chrome-stable) npm run test:ci
```
Covers `AuthService` and the `SecurityComponent` (set / change / disable, password
match + length validation) alongside the existing suite.

### Firmware C unit tests (`http_auth`)

The tests are registered in `test/CMakeLists.txt` (`TEST_COMPONENTS`), so they build
with the standard unit-test app (see [unit_testing.md](unit_testing.md)). Run them on a
device (flash + `idf.py monitor`, per unit_testing.md), or locally in QEMU:
```bash
cd test          # or test-ci
idf.py fullclean # avoids a stale target in the cache
idf.py qemu      # builds, boots in QEMU, runs every test; Ctrl-] to exit
```
CI runs them automatically in QEMU via `.github/workflows/unittest.yml`. Expected output:
```
components/http_auth/test/test_http_auth.c:40:http_auth: verify_hash accepts the right password:PASS
...
85 Tests 0 Failures 0 Ignored
```
(85 = the `http_auth`, `stratum` and `asic` suites combined; the `http_auth` cases are
the 13 added here. An `abort()` after the summary is just the interactive test menu
hitting end-of-input under headless QEMU — it does not affect the results.)

### Full firmware build (compile/link check)

Confirms the auth component + server wiring + Angular UI all build into the real
image for the Bitaxe's chip:
```bash
idf.py set-target esp32s3
idf.py build
```
