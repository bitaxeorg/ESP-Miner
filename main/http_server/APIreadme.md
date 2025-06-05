# ESP32 Miner HTTP API Documentation

This document describes the REST API endpoints available in the ESP32 miner HTTP server. The API provides access to system information, configuration, themes, logging, and firmware updates.

## Base URL

All API endpoints are relative to the device's IP address:
- **Development**: `http://192.168.1.100` (example)
- **AP Mode**: `http://192.168.4.1` (default AP IP)

## Authentication & CORS

### Network Access Control
- API requests are restricted to private IP ranges when in STA mode
- All requests are allowed when in AP mode
- CORS headers are automatically added to responses

### CORS Headers
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

## API Endpoints

### System Information

#### GET `/api/system/info`
Get comprehensive system information including mining stats, hardware info, and configuration.

**Key Response Fields:**
- `hashRate`: Current actual hashrate in GH/s
- `expectedHashrate`: Theoretical maximum hashrate in GH/s based on current frequency and hardware configuration
- `temp`: ASIC temperature in Celsius
- `power`: Current power consumption in watts
- `voltage`: Input voltage in volts
- `frequency`: Current ASIC frequency in MHz
- `coreVoltage`: Target core voltage in millivolts
- `asicCount`: Number of ASIC chips
- `smallCoreCount`: Number of small cores per ASIC

**Response Example:**
```json
{
  "power": 45.2,
  "voltage": 12.1,
  "current": 3.7,
  "temp": 65,
  "vrTemp": 58,
  "hashRate": 120.5,
  "expectedHashrate": 134.4,
  "bestDiff": "1.2K",
  "bestSessionDiff": "2.1K", 
  "stratumDiff": 16.0,
  "isUsingFallbackStratum": 0,
  "freeHeap": 180000,
  "coreVoltage": 1200,
  "coreVoltageActual": 1198,
  "frequency": 500,
  "ssid": "MyWiFi",
  "macAddr": "AA:BB:CC:DD:EE:FF",
  "hostname": "esp-miner",
  "wifiStatus": "Connected",
  "sharesAccepted": 150,
  "sharesRejected": 2,
  "uptimeSeconds": 3600,
  "asicCount": 1,
  "smallCoreCount": 672,
  "ASICModel": "BM1368",
  "stratumURL": "stratum+tcp://pool.example.com",
  "fallbackStratumURL": "stratum+tcp://backup.example.com",
  "stratumPort": 4334,
  "fallbackStratumPort": 4334,
  "stratumUser": "username.worker",
  "fallbackStratumUser": "username.worker2",
  "version": "1.0.0",
  "idfVersion": "v5.1.1",
  "boardVersion": "v1.0",
  "runningPartition": "factory",
  "flipscreen": 1,
  "overheat_mode": 0,
  "invertscreen": 0,
  "invertfanpolarity": 1,
  "autofanspeed": 1,
  "fanspeed": 50,
  "fanrpm": 3000,
  "autotune": 1,
  "autotune_preset": "balance",
  "serialnumber": "ACS240001"
}
```

### System Configuration

#### PATCH `/api/system`
Update system configuration parameters.

**Request Body Example:**
```json
{
  "stratumURL": "stratum+tcp://newpool.example.com",
  "stratumPort": 4334,
  "stratumUser": "newuser.worker",
  "stratumPassword": "password",
  "fallbackStratumURL": "stratum+tcp://backup.example.com",
  "fallbackStratumPort": 4334,
  "fallbackStratumUser": "backup.worker",
  "fallbackStratumPassword": "password",
  "ssid": "NewWiFi",
  "wifiPass": "password123",
  "hostname": "my-miner",
  "coreVoltage": 1200,
  "frequency": 500,
  "flipscreen": 1,
  "overheat_mode": 0,
  "invertscreen": 0,
  "invertfanpolarity": 1,
  "autofanspeed": 1,
  "fanspeed": 60,
  "autotune": 1,
  "presetName": "efficiency"
}
```

**Response Example:**
```json
{
  "status": "success",
  "message": "Settings updated successfully",
  "updatedSettings": {
    "frequency": 500,
    "coreVoltage": 1200,
    "autofanspeed": 1,
    "fanspeed": 60,
    "stratumURL": "stratum+tcp://newpool.example.com",
    "stratumPort": 4334,
    "stratumUser": "newuser.worker",
    "stratumPassword": "***",
    "hostname": "my-miner",
    "presetName": "efficiency",
    "presetApplied": true
  },
  "timestamp": 1706798400
}
```

**Response Fields:**
- `status`: "success" indicates all settings were processed successfully
- `message`: Human-readable status message
- `updatedSettings`: Object containing only the settings that were actually changed
- `timestamp`: Unix timestamp of when the update occurred

**Notes:**
- Only settings that were included in the request and successfully updated are returned in `updatedSettings`
- Passwords are masked with "***" in the response for security
- If a preset is applied, `presetApplied` indicates whether it was successful
- The response is logged to the database as a settings update event

#### OPTIONS `/api/system`
Get allowed methods for system endpoint.

### System Control

#### OPTIONS `/api/system/restart`
Get allowed methods for restart endpoint (restart functionality currently commented out).

### Theme Management

#### GET `/api/themes`
Get list of available themes.

**Response Example:**
```json
{
  "themes": [
    {
      "name": "THEME_ACS_DEFAULT",
      "description": "ACS Default green theme",
      "version": "1.0"
    },
    {
      "name": "THEME_BITAXE_RED", 
      "description": "Bitaxe red theme",
      "version": "1.0"
    },
    {
      "name": "THEME_SOLO_MINING_CO",
      "description": "Solo Mining Co theme",
      "version": "1.0"
    }
  ],
  "lastUpdated": 1706798400
}
```

#### GET `/api/themes/current`
Get current active theme configuration.

**Response Example:**
```json
{
  "themeName": "THEME_ACS_DEFAULT",
  "primaryColor": "#A7F3D0",
  "secondaryColor": "#A7F3D0", 
  "backgroundColor": "#161F1B",
  "textColor": "#A7F3D0",
  "borderColor": "#A7F3D0"
}
```

#### PATCH `/api/themes/{themeName}`
Change active theme.

**Path Parameters:**
- `themeName`: Theme identifier (e.g., "THEME_BITAXE_RED")

**Response Example:**
```json
{
  "themeName": "THEME_BITAXE_RED",
  "primaryColor": "#F80421",
  "secondaryColor": "#FC4D62",
  "backgroundColor": "#070D17", 
  "textColor": "#F80421",
  "borderColor": "#FC4D62"
}
```

#### OPTIONS `/api/themes`
Get allowed methods for themes endpoint.

### Event Logging

#### GET `/api/logs/recent`
Get recent system event logs.

**Query Parameters:**
- `limit` (optional): Maximum number of events to return (default: 50, max: 100)

**Response Example:**
```json
{
  "events": [
    {
      "timestamp": 1706798400,
      "type": "system",
      "level": "info",
      "message": "System started",
      "data": {"bootTime": 1706798400}
    },
    {
      "timestamp": 1706798460,
      "type": "theme",
      "level": "info", 
      "message": "Theme changed",
      "data": {"previousTheme": "THEME_ACS_DEFAULT", "newTheme": "THEME_BITAXE_RED"}
    },
    {
      "timestamp": 1706798520,
      "type": "mining",
      "level": "info",
      "message": "Mining status update",
      "data": {"hashrate": 120.5, "temperature": 65}
    }
  ],
  "count": 3
}
```

#### GET `/api/logs/errors`
Get persistent error logs (errors and critical events only).

**Query Parameters:**
- `limit` (optional): Maximum number of errors to return (default: all errors)

**Response Example:**
```json
{
  "errors": [
    {
      "timestamp": 1706798600,
      "type": "system",
      "level": "error",
      "message": "ASIC communication failed",
      "data": {"attempts": 3, "lastResponse": "timeout"}
    },
    {
      "timestamp": 1706798700,
      "type": "network",
      "level": "critical",
      "message": "Pool connection lost",
      "data": {"reconnectAttempts": 5, "poolUrl": "stratum+tcp://pool.example.com"}
    }
  ],
  "count": 2,
  "totalErrors": 15,
  "lastError": 1706798700
}
```

#### GET `/api/logs/critical`
Get persistent critical logs (critical events only).

**Query Parameters:**
- `limit` (optional): Maximum number of critical events to return (default: all critical events)

**Response Example:**
```json
{
  "critical": [
    {
      "timestamp": 1706798600,
      "type": "power",
      "level": "critical",
      "message": "Overheat mode activated",
      "data": {"chipTemp": 85.0, "threshold": 80, "device": "DEVICE_MAX"}
    },
    {
      "timestamp": 1706798700,
      "type": "system",
      "level": "critical",
      "message": "Hardware failure detected",
      "data": {"component": "ASIC", "errorCode": "0xFF"}
    }
  ],
  "count": 2,
  "totalCritical": 8,
  "lastCritical": 1706798700
}
```

**Key Features:**
- **Critical-Only**: Only contains events with "critical" level
- **Triple Logging**: Critical events are automatically logged to recent logs, error logs, AND critical logs
- **Dedicated Storage**: Separate persistent storage for the most severe issues
- **Emergency Monitoring**: Designed for tracking system-threatening events

**Event Types:**
- `system`: System-level events (startup, shutdown, errors)
- `theme`: Theme change events
- `mining`: Mining-related events (hashrate, shares, etc.)
- `network`: Network connectivity events
- `power`: Power management events
- `user`: User-initiated actions

**Log Levels:**
- `debug`: Detailed debugging information
- `info`: General information messages
- `warning`: Warning conditions
- `error`: Error conditions
- `critical`: Critical system errors

### Firmware Updates

#### POST `/api/system/OTA`
Upload new firmware for over-the-air update.

**Content-Type:** `application/octet-stream`
**Body:** Binary firmware file

**Response:** 
- Success: "Firmware update complete, rebooting now!"
- Error: HTTP error with description

**Notes:**
- Not allowed in AP mode
- Device will reboot after successful update
- Validates firmware before applying

#### POST `/api/system/OTAWWW`
Upload new web UI files to update the web interface.

**Content-Type:** `application/octet-stream`
**Body:** Binary SPIFFS image file

**Response:**
- Success: "WWW update complete"
- Error: HTTP error with description

**Notes:**
- Not allowed in AP mode
- Updates the www partition with new web interface files

### WebSocket

#### GET `/api/ws`
WebSocket connection for real-time log streaming.

**Protocol:** WebSocket
**Usage:** Provides real-time streaming of system logs

**Connection Example:**
```javascript
const ws = new WebSocket('ws://192.168.1.100/api/ws');
ws.onmessage = function(event) {
    console.log('Log:', event.data);
};
```

### Recovery Mode

#### GET `/recovery`
Access recovery mode interface.

**Response:** HTML recovery page

**Usage:** Accessible when the main web interface fails to load

### Static Files

#### GET `/*`
Serve static web interface files.

**Response:** HTML, CSS, JavaScript, images, etc.

**Notes:**
- Serves compressed (.gz) files when available
- Automatically redirects to root for captive portal detection
- Sets appropriate content types and caching headers

## Error Handling

### HTTP Status Codes

- **200 OK**: Successful request
- **302 Temporary Redirect**: Redirect to captive portal
- **400 Bad Request**: Invalid request data
- **401 Unauthorized**: Access denied (network restriction)
- **404 Not Found**: Endpoint not found (redirects to captive portal)
- **500 Internal Server Error**: Server error

### Error Response Format

```json
{
  "error": "Error description"
}
```

## Rate Limiting

No explicit rate limiting is implemented, but the server has limits on:
- Maximum 10 concurrent connections
- Maximum 20 URI handlers
- Request timeout handling

## Security Considerations

1. **Network Access**: API access is restricted to private IP ranges in STA mode
2. **AP Mode Protection**: Full access only when device is in AP mode
3. **Input Validation**: JSON parsing with error handling
4. **Firmware Validation**: OTA updates include validation before applying
5. **Memory Protection**: Bounded input sizes and memory allocation

## Usage Examples

### JavaScript/Fetch API

```javascript
// Get system info
const response = await fetch('/api/system/info');
const systemInfo = await response.json();
console.log('Hash rate:', systemInfo.hashRate);

// Change theme
await fetch('/api/themes/THEME_BITAXE_RED', {
    method: 'PATCH'
});

// Update configuration
const updateResponse = await fetch('/api/system', {
    method: 'PATCH',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
        frequency: 550,
        coreVoltage: 1250
    })
});
const updateResult = await updateResponse.json();
console.log('Update status:', updateResult.status);
console.log('Updated settings:', updateResult.updatedSettings);

// Get recent logs
const logs = await fetch('/api/logs/recent?limit=10');
const logData = await logs.json();
console.log('Recent events:', logData.events);

// Get error logs
const errorLogs = await fetch('/api/logs/errors?limit=20');
const errorData = await errorLogs.json();
console.log('Error events:', errorData.errors);
console.log('Total errors:', errorData.totalErrors);

// Get critical logs
const criticalLogs = await fetch('/api/logs/critical?limit=10');
const criticalData = await criticalLogs.json();
console.log('Critical events:', criticalData.critical);
console.log('Total critical:', criticalData.totalCritical);
```

### cURL Examples

```bash
# Get system information
curl -X GET http://192.168.1.100/api/system/info

# Change theme
curl -X PATCH http://192.168.1.100/api/themes/THEME_BITAXE_RED

# Update configuration
curl -X PATCH http://192.168.1.100/api/system \
  -H "Content-Type: application/json" \
  -d '{"frequency": 550, "coreVoltage": 1250}'

# Get recent logs
curl -X GET "http://192.168.1.100/api/logs/recent?limit=20"

# Get error logs
curl -X GET "http://192.168.1.100/api/logs/errors?limit=50"

# Get critical logs
curl -X GET "http://192.168.1.100/api/logs/critical?limit=25"

# Upload firmware (example)
curl -X POST http://192.168.1.100/api/system/OTA \
  --data-binary @firmware.bin \
  -H "Content-Type: application/octet-stream"
``` 