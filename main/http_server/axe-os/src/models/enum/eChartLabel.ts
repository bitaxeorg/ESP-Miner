export enum eChartLabel {
    hashrate = 'Hashrate',
    hashrate_1m = 'Hashrate 1m',
    hashrate_10m = 'Hashrate 10m',
    hashrate_1h = 'Hashrate 1h',
    asicTemp = 'ASIC Temp',
    errorPercentage = 'Error %',
    vrTemp = 'VR Temp',
    asicVoltage = 'ASIC Voltage',
    voltage = 'Voltage',
    power = 'Power',
    current = 'Current',
    fanSpeed = 'Fan Speed',
    fanRpm = 'Fan RPM',
    fan2Rpm = 'Fan 2 RPM',
    wifiRssi = 'Wi-Fi RSSI',
    freeHeap = 'Free Heap',
    responseTime = 'Response Time',
    none = 'None'
}

export function chartLabelValue(enumKey: string) {
  return Object.entries(eChartLabel).find(([key, val]) => key === enumKey)?.[1];
}

export function chartLabelKey(value: eChartLabel): string {
  return Object.keys(eChartLabel)[Object.values(eChartLabel).indexOf(value)];
}
