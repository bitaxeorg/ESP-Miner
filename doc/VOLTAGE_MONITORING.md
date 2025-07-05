# Voltage Monitoring Feature Documentation

## Overview

The voltage monitoring feature provides real-time monitoring of individual ASIC voltages, enabling better diagnostics and performance optimization.

## Architecture

### Hardware Layer
- **ADS1115**: 16-bit ADC for precise voltage measurements
- **I2C Interface**: Communication with ESP32
- **Voltage Dividers**: Scale ASIC voltages to ADC input range

### Firmware Layer
- Polls ADS1115 at configurable intervals
- Calculates per-ASIC and chain statistics
- Exposes data via REST API

### UI Layer
- Angular component for visualization
- Real-time updates via polling
- Responsive grid layout

## Installation

### Hardware Setup

1. Connect ADS1115 to ESP32:
   ```
   ADS1115    ESP32
   VDD    ->  3.3V
   GND    ->  GND
   SCL    ->  GPIO22
   SDA    ->  GPIO21
   ```

2. Connect voltage dividers for each ASIC voltage rail

### Software Setup

1. Build the firmware:
   ```bash
   idf.py build
   ./merge_bin.sh ./esp-miner-merged.bin
   ```

2. Flash to device:
   ```bash
   bitaxetool --firmware ./esp-miner-merged.bin
   ```

## API Reference

### GET /api/voltage

Returns voltage monitoring status and measurements.

**Response:**
```json
{
  "enabled": true,
  "hardware_present": true,
  "scan_interval_ms": 1000,
  "chains": [
    {
      "chain_id": 0,
      "total_voltage": 14.4,
      "average_voltage": 1.2,
      "min_voltage": 1.15,
      "max_voltage": 1.25,
      "failed_asics": 0,
      "suggested_frequency": 485,
      "asics": [
        {
          "id": 0,
          "voltage": 1.2,
          "valid": true
        }
      ]
    }
  ]
}
```

## UI Components

### VoltageMonitorComponent

Located at: `main/http_server/axe-os/src/app/components/voltage-monitor/`

**Features:**
- Grid layout showing all ASICs
- Color coding based on voltage thresholds
- Chain-level statistics
- Auto-refresh capability

### Color Coding Logic

- **Green (Healthy)**: Voltage between 1.1V - 1.4V
- **Amber (Degraded)**: Voltage between 0.8V - 1.1V or above 1.4V  
- **Red (Fault)**: Voltage below 0.8V or invalid readings

## Configuration

### Voltage Thresholds

Edit thresholds in `voltage-monitor.component.ts`:
```typescript
if (!asic.valid || asic.voltage < 0.8) {
  status = 'fault';
} else if (asic.voltage < 1.1 || asic.voltage > 1.4) {
  status = 'degraded';
}
```

### Refresh Interval

Modify in `voltage-monitor.component.ts`:
```typescript
private refreshInterval = 5000; // milliseconds
```

## Development

### Testing Without Hardware

Use the included mock server:

```bash
cd mock-server
npm install
npm start -- --chains 2 --asics 12
```

### Building the UI

```bash
cd main/http_server/axe-os
npm install
npm run build
```

## Troubleshooting

### Hardware Not Detected
- Check I2C connections
- Verify ADS1115 address (default: 0x48)
- Check pull-up resistors on I2C lines

### No Voltage Readings
- Verify voltage divider ratios
- Check ADC channel configuration
- Ensure proper grounding

## Future Enhancements

- [ ] Historical voltage graphs
- [ ] Voltage-based frequency optimization
- [ ] Alert thresholds and notifications
- [ ] Export voltage data to CSV
- [ ] Per-ASIC temperature correlation

## Contributing

Please submit issues and pull requests to the [feature/voltage-monitoring-advanced](https://github.com/ahmedalalousi/ESP-Miner/tree/feature/voltage-monitoring-advanced) branch.
