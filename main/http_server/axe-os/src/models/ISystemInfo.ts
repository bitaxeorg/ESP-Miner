import { eASICModel } from './enum/eASICModel';

interface ISharesRejectedStat {
    message: string;
    count: number;
}

export interface ISystemInfo {
    flipscreen: number;
    invertscreen: number;
    power: number,
    voltage: number,
    current: number,
    temp: number,
    vrTemp: number,
    hashRate: number,
    bestDiff: string,
    bestSessionDiff: string,
    freeHeap: number,
    coreVoltage: number,
    hostname: string,
    macAddr: string,
    ssid: string,
    wifiStatus: string,
    apEnabled: number,
    sharesAccepted: number,
    sharesRejected: number,
    sharesRejectedReasons: ISharesRejectedStat[];
    uptimeSeconds: number,
    asicCount: number,
    smallCoreCount: number,
    ASICModel: eASICModel,
    stratumURL: string,
    stratumPort: number,
    fallbackStratumURL: string,
    fallbackStratumPort: number,
    isUsingFallbackStratum: boolean,
    stratumUser: string,
    fallbackStratumUser: string,
    frequency: number,
    version: string,
    idfVersion: string,
    boardVersion: string,
    invertfanpolarity: number,
    autofanspeed: number,
    fanspeed: number,
    fanrpm: number,
    coreVoltageActual: number,

    boardtemp1?: number,
    boardtemp2?: number,
    overheat_mode: number,
    error?: string; // Optional field for error messages
    overclockEnabled?: number

}

export const DEFAULT_SYSTEM_INFO: ISystemInfo = {
    flipscreen: 0,
    invertscreen: 0,
    power: 0,
    voltage: 0,
    current: 0,
    temp: 0,
    vrTemp: 0,
    hashRate: 0,
    bestDiff: '0',
    bestSessionDiff: '0',
    freeHeap: 0,
    coreVoltage: 0,
    hostname: 'N/A',
    macAddr: '00:00:00:00:00:00',
    ssid: 'N/A',
    wifiStatus: 'N/A',
    apEnabled: 0,
    sharesAccepted: 0,
    sharesRejected: 0,
    sharesRejectedReasons: [],
    uptimeSeconds: 0,
    asicCount: 0,
    smallCoreCount: 0,
    ASICModel: eASICModel.BM1366,
    stratumURL: '',
    stratumPort: 0,
    fallbackStratumURL: '',
    fallbackStratumPort: 0,
    isUsingFallbackStratum: false,
    stratumUser: '',
    fallbackStratumUser: '',
    frequency: 0,
    version: 'N/A',
    idfVersion: 'N/A',
    boardVersion: 'N/A',
    invertfanpolarity: 0,
    autofanspeed: 0,
    fanspeed: 0,
    fanrpm: 0,
    coreVoltageActual: 0,
    boardtemp1: 0,
    boardtemp2: 0,
    overheat_mode: 0,
    error: ''
  } as const;
