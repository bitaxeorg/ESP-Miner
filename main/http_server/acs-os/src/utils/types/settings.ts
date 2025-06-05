export interface Settings {
  /** ASIC model identifier */
  ASICModel: string;
  /** Whether AP mode is enabled (0=no, 1=yes) */
  apEnabled: number;
  /** Number of ASICs detected */
  asicCount: number;
  /** Automatic fan speed control (0=manual, 1=auto) */
  autofanspeed: number;
  /** Best difficulty achieved */
  bestDiff: string;
  /** Best difficulty achieved in current session */
  bestSessionDiff: string;
  /** Hardware board version */
  boardVersion: string;
  /** Configured ASIC core voltage */
  coreVoltage: number;
  /** Actual measured ASIC core voltage */
  coreVoltageActual: number;
  /** Current draw in milliamps */
  current: number;
  /** Fallback stratum server port */
  fallbackStratumPort: number;
  /** Fallback stratum server URL */
  fallbackStratumURL: string;
  /** Fallback stratum username */
  fallbackStratumUser: string;
  /** Current fan speed in RPM */
  fanrpm: number;
  /** Current fan speed percentage */
  fanspeed: number;
  /** Target Temperature for the PID Controller */
  temptarget: number;
  /** Screen flip setting (0=normal, 1=flipped) */
  flipscreen: number;
  /** Available heap memory in bytes */
  freeHeap: number;
  /** ASIC frequency in MHz */
  frequency: number;
  /** Current hash rate */
  hashRate: number;
  /** Device hostname */
  hostname: string;
  /** ESP-IDF version */
  idfVersion: string;
  /** Screen invert setting (0=normal, 1=inverted) */
  invertscreen: number;
  /** Whether PSRAM is available (0=no, 1=yes) */
  isPSRAMAvailable: number;
  /** Whether using fallback stratum (0=no, 1=yes) */
  isUsingFallbackStratum: number;
  /** Device MAC address */
  macAddr: string;
  /** Maximum power draw of the board in watts */
  maxPower: number;
  /** Nominal board voltage */
  nominalVoltage: number;
  /** Overheat protection mode */
  overheat_mode: number;
  /** Set custom voltage/frequency in AxeOS */
  overclockEnabled: number;
  /** Power consumption in watts */
  power: number;
  /** Voltage regulator fault reason, if any */
  power_fault?: string;
  /** Currently active OTA partition */
  runningPartition: string;
  /** Number of accepted shares */
  sharesAccepted: number;
  /** Number of rejected shares */
  sharesRejected: number;
  /** Reason(s) shares were rejected */
  sharesRejectedReasons: SharesRejectedReason[];
  /** Number of small cores */
  smallCoreCount: number;
  /** Connected WiFi network SSID */
  ssid: string;
  /** Current stratum difficulty */
  stratumDiff: number;
  /** Primary stratum server port */
  stratumPort: number;
  /** Primary stratum server URL */
  stratumURL: string;
  /** Primary stratum username */
  stratumUser: string;
  /** Average chip temperature */
  temp: number;
  /** System uptime in seconds */
  uptimeSeconds: number;
  /** Firmware version */
  version: string;
  /** Input voltage */
  voltage: number;
  /** Voltage regulator temperature */
  vrTemp: number;
  /** WiFi connection status */
  wifiStatus: string;
  /** WiFi signal strength */
  wifiRSSI: number;
}

export interface SharesRejectedReason {
  reason: string;
  count: number;
}
