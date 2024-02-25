import { eASICModel } from './enum/eASICModel';

export interface ISystemInfo {

    flipscreen: number;
    invertscreen: number;
    power: number,
    voltage: number,
    current: number,
    fanSpeed: number,
    temp: number,
    hashRate: number,
    bestDiff: string,
    freeHeap: number,
    coreVoltage: number,
    ssid: string,
    wifiStatus: string,
    sharesAccepted: number,
    sharesRejected: number,
    uptimeSeconds: number,
    ASICModel: eASICModel,
    stratumURL: string,
    stratumPort: number,
    stratumUser: string,
    frequency: number,
    version: string,
    boardVersion: string,
    invertfanpolarity: number,
    autofanspeed: number,
    fanspeed: number,
    coreVoltageActual: number,

    boardtemp1?: number,
    boardtemp2?: number
}