import { eASICModel } from './enum/eASICModel';

interface ISharesRejectedStat {
    message: string;
    count: number;
}

export interface ISystemInfo {

    display: string;
    flipScreen: number;
    invertScreen: number;
    displayTimeout: number;
    power: number,
    voltage: number,
    current: number,
    temp: number,
    vrTemp: number,
    maxPower: number,
    nominalVoltage: number,
    hashrate: number,
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
    asicModel: eASICModel,
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
    autoFanSpeed: number,
    fanSpeed: number,
    tempTarget: number,
    fanRPM: number,
    statsLimit: number,
    statsDuration: number,
    coreVoltageActual: number,

    boardtemp1?: number,
    boardtemp2?: number,
    overheatMode: number,
    powerFault?: string
    overclockEnabled?: number
}
