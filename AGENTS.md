# ESP-Miner Development Guide for Agents

Welcome to the ESP-Miner repository. This document provides a high-level overview of the project structure, build instructions, and CI setup to help you navigate and contribute effectively.

## Project Overview
ESP-Miner is the firmware powering the Bitaxe open-source Bitcoin ASIC miners. It is built on the ESP-IDF framework (v5.x+) for the ESP32-S3 and includes a modern web interface (Axe-OS) built with Angular.

## Repository Structure

```text
.
├── .github/workflows/          # CI/CD pipelines (GitHub Actions)
├── bootloader_components/      # Custom components for the 2nd stage bootloader
├── components/                 # Shared application components (asic, stratum, etc.)
├── main/                       # Main application source code
│   └── http_server/            # C-based HTTP server logic
│       └── axe-os/             # Axe-OS Angular Frontend
├── config-*.cvs                # Hardware-specific configuration files
├── partitions.csv              # Flash partition layout
└── sdkconfig                   # Project-wide ESP-IDF configuration
```

## Build Instructions

### 1. Firmware (Main Project)
The project uses the standard ESP-IDF build system. Ensure you are using ESP-IDF v5.5.1 (or compatible).

**Prerequisites:**
- ESP-IDF v5.5.1 environment sourced.
- Node.js (v22+) and npm for the frontend bundle.

**Commands:**
```bash
# Source the environment
. ~/esp/v5.5.1/esp-idf/export.sh

# Build the project (automatically builds Axe-OS and generates binaries)
idf.py build

# Flash to device
idf.py build flash
```
*Note: The Axe-OS frontend is automatically built and compressed into `www.bin` as part of the main `idf.py build` process.*

### 2. Axe-OS (Frontend)
The frontend is a standalone Angular application located in `main/http_server/axe-os`.

**Commands:**
```bash
cd main/http_server/axe-os
npm install

# Build only the frontend
npm run build

# Run local development server
npm run start
```

## Testing

### Internal C Components
Firmware unit tests are located in the `test/` directory.
```bash
idf.py build test
```

### Axe-OS Frontend
Angular unit tests use Karma and Jasmine. We use a specific CI command to generate JUnit reports.
```bash
cd main/http_server/axe-os
npm run test:ci
```

## CI/CD Setup

We use GitHub Actions for automated testing and releases.

### Key Workflows:
- **`unittest.yml`**: Runs both backend and frontend unit tests in parallel.
- **`build.yml`**: Verifies that the project compiles for all primary configurations.
- **`release.yml`**: Handles automated releases and binary packaging.

## AI Agent Tips
- **API Generation**: If you modify `openapi.yaml`, you **must** run `npm run generate:api` in the `axe-os` directory to update the TypeScript services.
- **PSRAM**: Bitaxe heavily relies on PSRAM. Always check `esp_psram_is_initialized()` before allocating large buffers in the backend.
