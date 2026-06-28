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
};

#endif // DEFAULT_SCREENS_H_
