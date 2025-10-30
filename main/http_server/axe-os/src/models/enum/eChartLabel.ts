export enum eChartLabel {
    hashrate = 'Hashrate',
    asicTemp = 'ASIC Temp',
    errorCount = 'Error Count',
    vrTemp = 'VR Temp',
    asicVoltage = 'ASIC Voltage',
    voltage = 'Voltage',
    power = 'Power',
    current = 'Current',
    fanSpeed = 'Fan Speed',
    fanRpm = 'Fan RPM',
    wifiRssi = 'Wi-Fi RSSI',
    freeHeap = 'Free Heap',
    none = 'None'
}

export function chartLabelValue(enumKey: string) {
  return Object.entries(eChartLabel).find(([key, val]) => key === enumKey)?.[1];
}

export function chartLabelKey(value: eChartLabel): string {
  return Object.keys(eChartLabel)[Object.values(eChartLabel).indexOf(value)];
}
