#ifndef DEFAULT_SCREENS_H_
#define DEFAULT_SCREENS_H_

// Default screen configurations for OLED display
// Each screen is a string with \n separating lines
// Variables in curly braces {variable} are replaced at runtime

#define DEFAULT_SCREENS_COUNT 4

static const char *default_screens[] = {
    // Screen 1: URLs (from SCR_URLS)
    "Stratum Host:\n{pool_url}\nIP Address:\n{ip}",

    // Screen 2: Stats (from SCR_STATS)
    "Gh/s: {hashrate}\nJ/Th: {efficiency}\nBest: {session_diff}/{best_diff}\nTemp: {asic_temp}°C",

    // Screen 3: Mining (from SCR_MINING)
    "Block: {block_height}\nDifficulty: {network_diff}\nScriptsig:\n{scriptsig}",

    // Screen 4: WiFi (from SCR_WIFI)
    "Wi-Fi Signal\nRSSI: {rssi} dBm\nSignal: {signal}\nUptime: {uptime}",

    // // Screen 5: Power Stats
    // "Volts: {voltage} V\nAmps: {amps} A\nFan: {fan}%\nRPM: {fan_rpm}",

    // // Screen 6: Mining Stats
    // "Shares: {shares_a}/{shares_r}\nJobs Received: {work_received}\nTime: {response_time}ms",

    // // Screen 7: System Info
    // "Frequency: {frequency}MHz\nExpected: {expected_hashrate}Gh/s\nPower: {power} W\nCore: {core_voltage}nV",

    // // Screen 8: Temperature Details
    // "Chip Temp: {temp}°C\nVR Temp: {vr_temp}°C\nTarget: {temp_target}°C",
};

#endif // DEFAULT_SCREENS_H_
