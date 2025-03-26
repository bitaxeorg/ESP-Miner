import { eASICModel } from './enum/eASICModel';

export interface ISystemInfo {

    flipScreen: number;
    invertScreen: number;
    power: number,
    voltage: number,
    current: number,
    temp: number,
    vrTemp: number,
    hashrate: number,
    bestDiff: string,
    bestSessionDiff: string,
    freeHeap: number,
    coreVoltage: number,
    hostname: string,
    macAddr: string,
    ssid: string,
    wifiStatus: string,
    sharesAccepted: number,
    sharesRejected: number,
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
    invertFanPolarity: number,
    autoFanSpeed: number,
    fanSpeed: number,
    fanRPM: number,
    coreVoltageActual: number,

    boardtemp1?: number,
    boardtemp2?: number,
    overheatMode: number
}
