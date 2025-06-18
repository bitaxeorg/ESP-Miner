# Autotune and Presets Documentation

## Overview

The ESP miner implements two complementary systems for power management optimization:

1. **Presets System**: Pre-configured settings for different performance profiles
2. **Autotuning Logic**: Dynamic adjustment of mining parameters based on real-time conditions

## Presets System

### Device Model Support

The presets system supports four device models, each with tailored configurations:

- **DEVICE_MAX**: 1x BM1397 Asic from the S17 series
- **DEVICE_ULTRA**: 1x BM1366 Asic from S19XP 
- **DEVICE_SUPRA**: 1x BM1368 Asic from S21
- **DEVICE_GAMMA**: 1x BM1370 Asic from S21 Pro

### Preset Profiles

Each device model supports three preset profiles:

#### Quiet Mode
- **Purpose**: Low power consumption and noise
- **Characteristics**: Conservative voltage and frequency settings
- **Use Case**: Home mining, noise-sensitive environments

#### Balanced Mode  
- **Purpose**: Optimal performance-to-efficiency ratio
- **Characteristics**: Moderate voltage and frequency settings
- **Use Case**: General purpose mining

#### Turbo Mode
- **Purpose**: Maximum performance
- **Characteristics**: High voltage and frequency settings
- **Use Case**: Maximum hashrate scenarios

### Preset Configuration Examples

| Device Model | Profile | Voltage (mV) | Frequency (MHz) | Fan Speed (%) |
|--------------|---------|--------------|-----------------|---------------|
| DEVICE_MAX   | quiet   | 1100         | 450             | 50            |
| DEVICE_MAX   | balanced| 1200         | 550             | 65            |
| DEVICE_MAX   | turbo   | 1400         | 750             | 100           |
| DEVICE_ULTRA | quiet   | 1130         | 420             | 25            |
| DEVICE_ULTRA | balanced| 1190         | 490             | 35            |
| DEVICE_ULTRA | turbo   | 1250         | 625             | 95            |

### Preset Application Process

When a preset is applied:

1. **Safety First**: Fan speed is temporarily set to 100% for safety
2. **Parameter Setting**: Voltage, frequency, and fan speed are configured
3. **NVS Storage**: Settings are persisted to non-volatile storage
4. **Autotune Enable**: Autotune flag is activated
5. **Logging**: Configuration change is logged to database

### NVS Configuration Keys

- `NVS_CONFIG_ASIC_VOLTAGE`: Core voltage in millivolts
- `NVS_CONFIG_ASIC_FREQ`: ASIC frequency in MHz
- `NVS_CONFIG_FAN_SPEED`: Fan speed percentage
- `NVS_CONFIG_AUTOTUNE_PRESET`: Applied preset name
- `NVS_CONFIG_AUTOTUNE_FLAG`: Autotune enable/disable flag
- `NVS_CONFIG_AUTO_FAN_SPEED`: Automatic fan control flag

## Autotuning Logic

### Purpose

The autotuning system dynamically adjusts mining parameters to:
- Maintain optimal operating temperature (60°C target)
- Maximize hashrate within power and thermal limits
- Prevent overheating and hardware damage
- Optimize performance-per-watt efficiency

### Key Parameters

#### Target Values
- **Target Temperature**: 60°C (optimal ASIC operating temperature)
- **Temperature Tolerance**: ±2°C around target
- **Hashrate Target**: Calculated based on frequency and core count

#### Safety Limits
- **Maximum Power**: Device-specific power limit
- **Maximum Voltage**: Device-specific voltage limit  
- **Maximum Frequency**: Device-specific frequency limit
- **Thermal Limits**: 75°C throttle, 90°C emergency shutdown

### Timing Mechanism

#### Warmup Period
- **Duration**: 15 minutes (900 seconds) from startup
- **Purpose**: Allow system to reach thermal equilibrium
- **Behavior**: No adjustments during warmup if temperature < target

#### Adjustment Intervals
- **Normal Operation**: 5 minutes (300 seconds) when temp ≤ 68°C
- **High Temperature**: 5 seconds when temp > 68°C
- **Purpose**: Faster response during thermal stress

### Temperature-Based Adjustments

#### Temperature Within Target Range (-2°C to +2°C)
**Condition**: Temperature is 58°C - 62°C

**Action**: Hashrate optimization
- If hashrate < 80% of target: Increase voltage by 10mV
- If hashrate ≥ 80% of target: No adjustment needed

#### Temperature Below Target (< -2°C)
**Condition**: Temperature < 58°C

**Actions**: Performance increase
- Increase frequency by 2% (if within limits)
- Increase voltage by 0.2% (if within limits)
- Check power consumption limits before adjustment

**Safety Checks**:
- Frequency must be below maximum frequency limit
- Power consumption must be below maximum power limit
- Voltage must be below maximum voltage limit

#### Temperature Above Target (> +2°C)  
**Condition**: Temperature > 62°C

**Actions**: Performance reduction
- Decrease frequency by 2%
- Decrease voltage by 0.2%

**Purpose**: Reduce heat generation to bring temperature back to target

### Safety and Monitoring

#### Prerequisites for Autotune Operation
- Temperature sensor must be initialized (temp ≠ 255)
- Hashrate must be > 0 (system actively mining)
- Autotune flag must be enabled in NVS configuration

#### Overheat Protection
- **Throttle Temperature**: 75°C - Triggers Overheat Mode
- **VR Temperature Limit**: 105°C throttle, 145°C maximum
- **Actions**: Fan to 100%, voltage/frequency reduction, system shutdown if needed

#### Startup Overheat Reset

When the device starts up and detects it is currently in overheat mode (from a previous session), it automatically performs a startup reset to ensure safe operation:

**Startup Reset Process**:
1. **Detection**: Check `NVS_CONFIG_OVERHEAT_MODE` flag during system initialization
2. **Safety Reset**: Apply "balanced" preset for all device models
3. **Flag Clearing**: Reset overheat mode flag to 0 for normal operation
4. **Logging**: Record startup overheat reset event in database
5. **Fallback Safety**: If preset application fails, apply conservative safe defaults:
   - Voltage: 1100mV
   - Frequency: 400MHz  
   - Fan Speed: 75%
   - Auto Fan Control: Enabled
   - Autotune: Enabled

**Benefits of Startup Reset**:
- **Safety First**: Ensures device starts with conservative, safe settings
- **Automatic Recovery**: No manual intervention required for startup
- **Consistent State**: Device always starts in a known good configuration
- **Thermal Protection**: Balanced preset provides optimal safety/performance ratio

#### Overheat Recovery System

The system implements a cyclical overheat recovery pattern with two types of recovery mechanisms:

##### Recovery Pattern
The system uses a **3-event cycle** that provides both automatic recovery and mandatory manual intervention:

| Event # | Recovery Type | Action |
|---------|---------------|--------|
| 1, 2    | Soft Recovery | Wait 5 minutes → Apply balanced preset → Restart ESP32 |
| 3       | Hard Recovery | Exit power management task → Manual restart required |
| 4, 5    | Soft Recovery | Wait 5 minutes → Apply balanced preset → Restart ESP32 |
| 6       | Hard Recovery | Exit power management task → Manual restart required |
| 7, 8    | Soft Recovery | Wait 5 minutes → Apply balanced preset → Restart ESP32 |
| 9       | Hard Recovery | Exit power management task → Manual restart required |

This pattern repeats indefinitely, ensuring regular manual intervention while allowing automatic recovery.

##### Soft Recovery Process
**Triggers**: Events 1, 2, 4, 5, 7, 8, etc. (non-multiples of 3)

**Actions**:
1. Increment `NVS_CONFIG_OVERHEAT_COUNT`
2. Set fan to 100% speed immediately
3. Turn off VCORE/ASIC power based on device type
4. Apply emergency safe settings:
   - Voltage: 1000mV
   - Frequency: 50MHz
   - Fan: 100%
   - Disable auto fan control
5. Wait 5 minutes for cooling
6. Apply "balanced" preset for safe recovery
7. Reset overheat mode flag
8. Restart ESP32 for clean state

##### Hard Recovery Process
**Triggers**: Events 3, 6, 9, etc. (multiples of 3)

**Actions**:
1. Increment `NVS_CONFIG_OVERHEAT_COUNT`
2. Set fan to 100% speed immediately
3. Turn off VCORE/ASIC power based on device type
4. Apply emergency safe settings (same as soft recovery)
5. **Terminate power management task** using `vTaskDelete(NULL)`
6. System remains in overheat mode until manual restart
7. Log critical event: "Overheat Mode Activated 3+ times, Restart Device Manually"

**Note**: `vTaskDelete(NULL)` cleanly terminates only the power management task while leaving the ESP32 system running in a safe state. This prevents automatic recovery and forces manual intervention.

##### Overheat Counter Tracking
- **NVS Key**: `NVS_CONFIG_OVERHEAT_COUNT`
- **Purpose**: Persistent tracking of overheat events across reboots
- **Increment**: Every overheat event (both soft and hard recovery)
- **Reset**: Manual only (requires user intervention or NVS reset)
- **Logging**: Counter value included in all overheat event logs

##### Benefits of Cyclical Pattern
- **Safety**: Regular manual intervention prevents runaway heating
- **Usability**: Automatic recovery for occasional thermal events
- **Tracking**: Complete overheat history maintained
- **Predictability**: Users know every 3rd event requires manual attention
- **Flexibility**: System gets multiple chances after manual resets

#### Data Logging
All autotune adjustments are logged to the database with:
- Timestamp and event type
- Current and target values
- Adjustment reasons and amounts
- System state information

### Algorithm Flow

```
1. Check Prerequisites
   ├── Autotune enabled?
   ├── Valid temperature reading?
   ├── Hashrate > 0?
   └── Outside warmup period?

2. Timing Check
   ├── Sufficient time elapsed since last adjustment?
   └── Use appropriate interval (normal vs high temp)

3. Temperature Analysis
   ├── Calculate temperature difference from target (60°C)
   └── Determine adjustment strategy

4. Parameter Adjustment
   ├── Within target range → Optimize hashrate
   ├── Below target → Increase performance  
   └── Above target → Decrease performance

5. Safety Validation
   ├── Check power limits
   ├── Check voltage limits
   ├── Check frequency limits
   └── Apply changes if safe

6. Logging and Monitoring
   ├── Log adjustment to database
   ├── Update NVS configuration
   └── Monitor for next cycle
```

### Integration with Hardware Control

#### Voltage Control
- **Module**: VCORE (TPS546 voltage regulator)
- **Range**: Device-specific minimum to maximum voltage
- **Precision**: 1mV increments
- **Response Time**: Immediate

#### Frequency Control  
- **Module**: BM1397 ASIC frequency control
- **Range**: 50 MHz to device maximum
- **Precision**: 1 MHz increments  
- **Response Time**: Requires frequency transition process

#### Temperature Monitoring
- **Sensor**: EMC2101 temperature controller
- **ASIC Temperature**: External sensor reading
- **VR Temperature**: TPS546 internal temperature
- **Update Rate**: Every 2 seconds (POLL_RATE)

### Performance Optimization Features

#### Adaptive Learning
- System learns optimal settings through operation
- Adjustments become more refined over time
- Historical performance data influences decisions

#### Power Efficiency
- Monitors power consumption vs hashrate
- Optimizes for maximum GH/s per watt
- Prevents power limit violations

#### Thermal Management
- Proactive temperature control
- Coordinated fan speed adjustment
- Prevents thermal damage to components

## Configuration and Usage

### Enabling Autotune
```c
nvs_config_set_u16(NVS_CONFIG_AUTOTUNE_FLAG, 1);  // Enable
nvs_config_set_u16(NVS_CONFIG_AUTOTUNE_FLAG, 0);  // Disable
```

### Applying Presets
```c
// Apply preset by name
apply_preset(DEVICE_MAX, "balanced");
apply_preset(DEVICE_ULTRA, "turbo");
apply_preset(DEVICE_SUPRA, "quiet");
```

### Overheat Mode Management
The system automatically manages overheat mode transitions:
```c
// Overheat mode is automatically detected on startup
uint16_t overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);

// Startup reset automatically applied if overheat_mode == 1
// This happens before power management tasks start

// Manual overheat mode reset (if needed)
nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);   // Clear overheat flag
apply_preset(device_model, "balanced");             // Apply safe preset
```

### Manual Override
Individual parameters can be manually set to override autotune:
```c
nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1200);  // Set voltage
nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 600);      // Set frequency
nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);   // Disable auto fan
```

## Best Practices

### Recommended Usage
1. **Initial Setup**: Start with "balanced" preset for your device model
2. **Break-in Period**: Allow 24-48 hours for thermal stabilization
3. **Environment Consideration**: Adjust for ambient temperature and ventilation
4. **Monitoring**: Watch logs for adjustment patterns and stability
5. **Overheat Recovery**: Device automatically resets to balanced preset on startup if previously in overheat mode

### Troubleshooting
- **Frequent Adjustments**: Check ambient temperature and cooling
- **Poor Hashrate**: Verify power supply capacity and connections
- **Overheating**: Improve ventilation or reduce ambient temperature
- **Instability**: Consider using "quiet" preset for more conservative operation
- **Startup Issues**: If device was in overheat mode, it automatically resets to balanced preset on startup - check logs for "Startup overheat mode reset" events

### Safety Considerations
- Never disable overheat protection
- Ensure adequate power supply capacity
- Monitor system logs regularly
- Maintain proper ventilation and cooling 