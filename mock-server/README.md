# ESP-Miner Mock Server

A Node.js mock server for testing the ESP-Miner voltage monitoring UI without physical hardware.

## Features

- Simulates voltage data for configurable number of chains and ASICs
- Generates realistic voltage readings with healthy, degraded, and fault states
- Provides all necessary API endpoints for the AxeOS UI
- Auto-updates data every 5 seconds to simulate real hardware

## Installation

```bash
npm install
```

## Usage

### Basic Usage

Start with default settings (2 chains, 12 ASICs per chain):

```bash
npm start
```

### Custom Configuration

```bash
node voltage-monitor-mock.js --chains 4 --asics 16 --port 3000
```

### Preset Configurations

```bash
npm run test-small   # 1 chain, 6 ASICs
npm run test-medium  # 2 chains, 12 ASICs each
npm run test-large   # 4 chains, 16 ASICs each
```

## API Endpoints

- `GET /api/voltage` - Voltage monitoring data
- `GET /api/system/info` - System information
- `GET /api/system/statistics/dashboard` - Time series statistics
- `GET /api/system/asic` - ASIC configuration
- `GET /api/swarm/info` - Swarm information (empty array)

## Data Simulation

The mock server simulates:
- 80% healthy ASICs (1.15-1.25V)
- 15% degraded ASICs (1.05-1.15V)
- 5% faulty ASICs (0.5-0.9V)

## Development

For auto-restart on file changes:

```bash
npm run dev
```

## Testing with AxeOS

1. Start the mock server
2. Configure your Angular development environment to proxy API calls to `http://localhost:3000`
3. Or temporarily update service URLs in the Angular app to point to the mock server
