# AxeOS

The Angular frontend for the Bitaxe open-source Bitcoin miner. All commands can be run either from this directory or from the **repository root** (commands are forwarded via the root `package.json`).

## Development server

### Mock data (no device required)

```bash
npm run start
```

Navigate to `http://localhost:4200/`. The app will use built-in mock data so you can develop the UI without a real device. HMR is enabled — style changes apply instantly, template/TypeScript changes trigger a full page reload.

### Live device (proxy to real hardware)

To connect the dev server to an actual Bitaxe on your local network:

```bash
BITAXE_IP=192.168.1.152 npm run start:proxy
```

- Replace `192.168.1.152` with your device's IP address.
- If `BITAXE_IP` is omitted, the proxy falls back to `http://192.168.1.100`.
- All `/api` HTTP and WebSocket traffic is forwarded to the device via `proxy.conf.js`.
- The `Origin` header is automatically rewritten to pass the device's private-network CORS check.

The `mock` flag in `src/environments/environment.ts` is `false` by default, so the real device API is used when running with the proxy. If you switch back to `npm run start` (no proxy), set `mock: true` in that file to re-enable mock data.

## Build

```bash
npm run build
```

Generates the production bundle, gzip-compresses assets, and writes a `version.txt`. Build artifacts are stored in `dist/axe-os/`.

## Running unit tests

```bash
npm run test:ci
```

Runs all Karma/Jasmine unit tests in a headless Chrome environment. Also accepts the plain `npm run test` variant for interactive (watch) mode.

```bash
export CHROME_BIN=/snap/bin/chromium   # adjust path for your system
npm run test
```

## Code generation

After modifying `../openapi.yaml`, regenerate the TypeScript API client:

```bash
npm run generate:api
```

This is also run automatically as part of `npm run build` and `npm run test:ci`.

## Further help

To get more help on the Angular CLI use `ng help` or check out the [Angular CLI Overview and Command Reference](https://angular.io/cli).
