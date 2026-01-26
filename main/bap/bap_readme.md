# Bitaxe Accessory Port (BAP) Protocol

## Overview

The Bitaxe Accessory Port (BAP) is a communication protocol designed for interacting with the Bitaxe. It provides a standardized way to monitor and control various aspects of the device through UART communication. BAP enables external accessories to request system information, subscribe to real-time data updates, and modify device settings.

## Protocol Specification

### Message Format

BAP messages follow a structured format similar to NMEA sentences:

```
$BAP,<COMMAND>,<PARAMETER>,<VALUE>*<CHECKSUM>\r\n
```

- **$**: Message start delimiter
- **BAP**: Protocol identifier
- **COMMAND**: Action to perform (3-4 characters)
- **PARAMETER**: Target parameter (variable length)
- **VALUE**: Associated value (optional, variable length)
- **CHECKSUM**: XOR checksum of the sentence body (2 hex characters)
- **\r\n**: Message end delimiters

### Command Types

| Command | Description | Direction |
|---------|-------------|-----------|
| `REQ` | Request a parameter value | Host → Device |
| `RES` | Response to a request or subscription update | Device → Host |
| `SUB` | Subscribe to periodic updates of a parameter | Host → Device |
| `UNSUB` | Unsubscribe from parameter updates | Host → Device |
| `SET` | Set a parameter value | Host → Device |
| `ACK` | Acknowledge successful command execution | Device → Host |
| `ERR` | Report an error condition | Device → Host |
| `CMD` | Send a command to the device | Host → Device |
| `STA` | Status update from the device | Device → Host |
| `LOG` | Log message from the device | Device → Host |

### Parameter Types

| Parameter | Description | Data Type | Example Value |
|-----------|-------------|-----------|---------------|
| `systemInfo` | Device model and ASIC information | String | "BM1366" |
| `hashrate` | Current hashrate in MH/s | Float | "20.5" |
| `temperature` | Chip temperature in Celsius | Float | "65.3" |
| `power` | Power consumption in watts | Float | "120.5" |
| `voltage` | Input voltage in volts | Float | "12.1" |
| `current` | Current draw in amps | Float | "10.2" |
| `shares` | Accepted/rejected shares | String | "123/2" |
| `frequency` | ASIC frequency in MHz | Float | "450.0" |
| `asic_voltage` | ASIC voltage in millivolts | Integer | "1200" |
| `ssid` | WiFi network SSID | String | "MyNetwork" |
| `password` | WiFi password | String | "secret123" |
| `fan_speed` | Fan speed percentage | Integer | "80" |
| `auto_fan` | Auto fan speed control | Boolean | "1" |
| `best_difficulty` | Best difficulty achieved | String | "1024" |
| `block_height` | Current network block height | Integer | "845123" |
| `wifi` | WiFi status information (ssid/password/rssi/ip) | String | Various |

## Implementation Details

### System Architecture

The BAP implementation is organized into several modules:

1. **Main Interface (`bap.c`)**
   - Initializes all BAP subsystems
   - Manages shared resources (queues, mutexes)
   - Coordinates initialization sequence

2. **Protocol Utilities (`bap_protocol.c`)**
   - Message formatting and parsing
   - Checksum calculation
   - Command/parameter string conversion

3. **UART Communication (`bap_uart.c`)**
   - UART initialization and configuration
   - Message sending and receiving
   - Communication task management

4. **Command Handlers (`bap_handlers.c`)**
   - Command parsing and dispatch
   - Implementation of specific command behaviors
   - Request and settings handling

5. **Subscription Management (`bap_subscription.c`)**
   - Subscription tracking
   - Periodic update scheduling
   - Timeout handling

### Key Features

#### Mode Awareness
BAP operates differently based on the device's WiFi connection status:
- **Connected Mode**: Full functionality available
- **AP Mode**: Limited functionality for WiFi configuration

#### Subscription Management
- Clients can subscribe to real-time parameter updates
- Configurable update intervals
- Automatic timeout after 5 minutes of inactivity
- Efficient resource management with mutex protection

#### Error Handling
- Checksum validation for message integrity
- Duplicate message filtering
- Comprehensive error reporting with descriptive messages

#### Thread Safety
- Mutex protection for shared resources
- Queue-based message passing
- Task isolation for different functionalities

## Usage Examples


### Requesting System Information
```
Host:   $BAP,REQ,systemInfo*3F\r\n
Device: $BAP,RES,deviceModel,BM1366*4A\r\n
Device: $BAP,RES,asicModel,BM1366*4B\r\n
Device: $BAP,RES,pool,example.com*5C\r\n
Device: $BAP,RES,poolPort,3333*5D\r\n
Device: $BAP,RES,poolUser,worker*5E\r\n
```

### Subscribing to Hashrate Updates
```
Host:   $BAP,SUB,hashrate,1000*4A\r\n
Device: $BAP,ACK,hashrate,subscribed*5B\r\n
Device: $BAP,RES,hashrate,20.5*4C\r\n
Device: $BAP,RES,hashrate,21.0*4D\r\n
...
Host:   $BAP,UNSUB,hashrate*4E\r\n
Device: $BAP,ACK,hashrate,unsubscribed*5F\r\n
```

### Setting ASIC Frequency
```
Host:   $BAP,SET,frequency,500.0*4A\r\n
Device: $BAP,ACK,frequency,500.0*5B\r\n
```

### Setting WiFi Credentials
```
Host:   $BAP,SET,ssid,MyNetwork*4A\r\n
Device: $BAP,ACK,ssid,MyNetwork*5B\r\n
Host:   $BAP,SET,password,secret123*4C\r\n
Device: $BAP,ACK,password,password_set*5D\r\n
Device: $BAP,STA,status,restarting*5E\r\n
```

## Configuration

BAP uses the following GPIO pins for UART communication:
- **RX**: Configurable via `CONFIG_GPIO_BAP_RX`
- **TX**: Configurable via `CONFIG_GPIO_BAP_TX`

UART settings:
- Baud rate: 115200
- Data bits: 8
- Parity: None
- Stop bits: 1

## Error Codes

| Error Code | Description |
|------------|-------------|
| `ap_mode_no_subscriptions` | Subscriptions not allowed in AP mode |
| `ap_mode_no_requests` | Requests not allowed in AP mode |
| `ap_mode_limited_settings` | Only WiFi settings allowed in AP mode |
| `missing_parameter` | Required parameter missing |
| `system_not_ready` | System not initialized |
| `invalid_range` | Parameter value outside valid range |
| `set_failed` | Failed to set parameter |
| `unsupported_parameter` | Parameter not supported |
| `subscription_timeout` | Subscription expired after 5 minutes |

## Development Guidelines

### Adding New Parameters

1. Add the parameter to the `bap_parameter_t` enum in `bap_protocol.h`
2. Add the string representation to `parameter_strings` in `bap_protocol.c`
3. Implement handling in `BAP_send_subscription_update()` if it's subscribable
4. Add handling in `BAP_handle_settings()` if it's configurable

***For a comprehensive view into adding a new parameter look into commit `e377b5c57f08c34c8b2894aceacb27180e44f52e` there was the best difficulty added.***

### Adding New Commands

1. Add the command to the `bap_command_t` enum in `bap_protocol.h`
2. Add the string representation to `command_strings` in `bap_protocol.c`
3. Register a handler in `BAP_handlers_init()` in `bap_handlers.c`
4. Implement the handler function

## Limitations

- Maximum message length: 256 characters
- Maximum 10 pending messages in send queue
- Subscription timeout: 5 minutes
- UART buffer threshold: 50% of buffer size

## Troubleshooting

### Common Issues

1. **No Response from Device**
   - Check UART wiring and pin configuration
   - Verify baud rate settings (115200)
   - Ensure proper power supply to the device

2. **Checksum Errors**
   - Verify message formatting
   - Check for data corruption on the communication line
   - Ensure proper termination characters (\r\n)

3. **Subscription Timeouts**
   - Resubscribe to parameters periodically
   - Check for communication interruptions
   - Verify device is not restarting

### Debugging Tips

- Enable verbose logging to see message flow
- Use a serial terminal to manually send commands
- Monitor UART buffer status for overflow conditions
- Check mutex acquisition failures in logs
