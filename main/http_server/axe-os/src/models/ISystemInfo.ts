import { eASICModel } from './enum/eASICModel';

interface ISharesRejectedStat {
    message: string;
    count: number;
}

export interface ISystemInfo {
    display: string;
    rotation: number;
    invertscreen: number;
    displayTimeout: number;
    power: number,
    voltage: number,
    current: number,
    temp: number,
    vrTemp: number,
    maxPower: number,
    nominalVoltage: number,
    hashRate: number,
    expectedHashrate: number,
    bestDiff: string,
    bestSessionDiff: string,
    freeHeap: number,
    coreVoltage: number,
    hostname: string,
    macAddr: string,
    ssid: string,
    wifiStatus: string,
    wifiRSSI: number,
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
    stratumUser: string,
    stratumDifficulty: number,
    stratumExtranonceSubscribe: number,
    fallbackStratumURL: string,
    fallbackStratumPort: number,
    fallbackStratumUser: string,
    fallbackStratumDifficulty: number,
    fallbackStratumExtranonceSubscribe: number,
    responseTime: number,
    isUsingFallbackStratum: boolean,
    frequency: number,
    version: string,
    idfVersion: string,
    boardVersion: string,
    autofanspeed: number,
    fanspeed: number,
    temptarget: number,
    fanrpm: number,
    statsFrequency: number,
    coreVoltageActual: number,

    boardtemp1?: number,
    boardtemp2?: number,
    overheat_mode: number,
    power_fault?: string
    overclockEnabled?: number
}
