export const CHART_LABELS = {
  hashrate: 'Hashrate',
  hashrate_1m: 'Hashrate 1m',
  hashrate_10m: 'Hashrate 10m',
  hashrate_1h: 'Hashrate 1h',
  asicTemp: 'ASIC Temp',
  asicTemp2: 'ASIC Temp 2',
  errorPercentage: 'Error %',
  vrTemp: 'VR Temp',
  asicVoltage: 'ASIC Voltage',
  voltage: 'Voltage',
  power: 'Power',
  current: 'Current',
  fanSpeed: 'Fan Speed',
  fanRpm: 'Fan RPM',
  fan2Rpm: 'Fan 2 RPM',
  wifiRssi: 'Wi-Fi RSSI',
  freeHeap: 'Free Heap',
  responseTime: 'Response Time',
  none: 'None',
} as const;

export type ChartMetric = keyof typeof CHART_LABELS;

export const CHART_LABEL_TO_METRIC: Record<string, ChartMetric> = Object.fromEntries(
  Object.entries(CHART_LABELS).map(([k, v]) => [v, k as ChartMetric])
);

export interface ChartUnitGroup {
  name: string;
  value: string;
  labels: ChartMetric[];
}

export const ChartUnitGroups: ChartUnitGroup[] = [
  { name: 'Hashrate (H/s)', value: 'hashrate', labels: ['hashrate', 'hashrate_1m', 'hashrate_10m', 'hashrate_1h'] },
  { name: 'Temperature (°C)', value: 'temperature', labels: ['asicTemp', 'asicTemp2', 'vrTemp'] },
  { name: 'Voltage (V)', value: 'voltage', labels: ['asicVoltage', 'voltage'] },
  { name: 'Power (W)', value: 'power', labels: ['power'] },
  { name: 'Current (A)', value: 'current', labels: ['current'] },
  { name: 'Fan Speed (RPM)', value: 'rpm', labels: ['fanRpm', 'fan2Rpm'] },
  { name: 'Percentage (%)', value: 'percentage', labels: ['fanSpeed', 'errorPercentage'] },
  { name: 'Wi-Fi RSSI (dBm)', value: 'rssi', labels: ['wifiRssi'] },
  { name: 'Free Heap (Bytes)', value: 'heap', labels: ['freeHeap'] },
  { name: 'Response Time (ms)', value: 'time', labels: ['responseTime'] },
  { name: 'None', value: 'none', labels: ['none'] }
];
