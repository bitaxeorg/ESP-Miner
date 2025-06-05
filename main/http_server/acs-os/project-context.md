# Project Context

## Summary

This project is a super lightweight web UI for an embedded device (ESP32-class) used in Bitcoin mining (Bitaxe/ESP-Miner). It's a replacement for an existing Angular-based frontend, served via the device's internal HTTP server. The new implementation uses **Preact** + **Vite** to minimize JS footprint and runtime memory usage. Target size is under **4MB total** (including firmware and web assets).

## Goals

- Replace Angular frontend with a smaller Preact-based UI
- Stay under 4MB flash size (UI + firmware)
- Minimize cold-start RAM usage
- Allow easy UX for interacting with miner settings, stats, logs
- Load fast over Wi-Fi from embedded HTTP server

## Tech Stack

- **Preact** (via `preact/compat`) instead of React
- **Vite** as bundler for minimal and fast builds
- **TailwindCSS** for utility-first styling
- **Custom hash-based routing** (no `react-router-dom`)
- **No state libraries** (simple `useState`, `useEffect`, and custom hooks)
- Optional: **Wouter** for minimal routing (\~2kB)

## Constraints

- Must fit in 4MB flash (including device firmware)
- Runs entirely from embedded HTTP server, no CDN, no external requests
- Must render usable UI even with slow/unstable Wi-Fi
- No usage of large libraries like `react-router-dom`, `lodash`, `moment`
- No SSR, no server APIs other than local HTTP endpoints

## Design

- Reuse Tailwind classes from ShadCN UI but **not** ShadCN component logic (Radix is incompatible with Preact)
- Rewrite any necessary components (Dialog, Input, etc) manually
- All routing is hash-based and defined in `routes.ts`
- App loads as a single `index.html` and defers all logic to the client

## Structure

```
/src
  /components      # Shared UI components (Button, Dialog, etc)
  /pages           # Top-level views by route
  /lib             # Utility functions and API helpers
  /hooks           # Custom Preact hooks
  /routes.ts       # Simple route-to-component mapping
  /main.tsx        # Entry point
  /app.tsx         # Root app shell and routing logic
```

## Build

```bash
npm install
npm run build
```

Generates precompressed `.gz` and `.br` assets using Vite + `vite-plugin-compression`. Artifacts are copied into ESP32 firmware filesystem.

## Firmware Integration

- The built assets go into the `main/http_server/dist` folder in the ESP-IDF repo
- Served using a basic file-server over HTTP
- Uses localStorage for persistent state (Wi-Fi config, hostname, etc)

## Future Considerations

- Potential support for OTA updates of UI
- Theme support via Tailwind tokens
- i18n support using native JSON + hook
- Svelte or HTMX variant if Preact ever becomes too heavy

---

This file is for AI-assisted context inside Cursor.
