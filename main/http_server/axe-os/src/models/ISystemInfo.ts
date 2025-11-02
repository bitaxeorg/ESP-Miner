interface ISharesRejectedStat {
    message: string;
    count: number;
}

interface IHashrateMonitorAsic {
    total: number;
    domains: number[];
    error: number;
}

interface IHashrateMonitor {
    asics: IHashrateMonitorAsic[];
    errorCount: number;
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
    temp2: number,
    vrTemp: number,
    maxPower: number,
    nominalVoltage: number,
    hashRate: number,
    expectedHashrate: number,
    bestDiff: number,
    bestSessionDiff: number,
    freeHeap: number,
    freeHeapInternal: number,
    freeHeapSpiram: number,
    coreVoltage: number,
    hostname: string,
    macAddr: string,
    ssid: string,
    wifiStatus: string,
    ipv4: string,
    ipv6: string,
    wifiRSSI: number,
    apEnabled: number,
    sharesAccepted: number,
    sharesRejected: number,
    sharesRejectedReasons: ISharesRejectedStat[];
    uptimeSeconds: number,
    smallCoreCount: number,
    ASICModel: string,
    stratumURL: string,
    stratumPort: number,
    stratumUser: string,
    stratumSuggestedDifficulty: number,
    stratumExtranonceSubscribe: number,
    fallbackStratumURL: string,
    fallbackStratumPort: number,
    fallbackStratumUser: string,
    fallbackStratumSuggestedDifficulty: number,
    fallbackStratumExtranonceSubscribe: number,
    poolDifficulty: number,
    responseTime: number,
    isUsingFallbackStratum: boolean,
    frequency: number,
    version: string,
    axeOSVersion: string,
    idfVersion: string,
    boardVersion: string,
    autofanspeed: number,
    minFanSpeed: number,
    fanspeed: number,
    temptarget: number,
    fanrpm: number,
    statsFrequency: number,
    coreVoltageActual: number,

    boardtemp1?: number,
    boardtemp2?: number,
    overheat_mode: number,
    power_fault?: string,
    overclockEnabled?: number,

    blockHeight?: number,
    scriptsig?: string,
    networkDifficulty?: string,

    hashrateMonitor: IHashrateMonitor,
    blockFound: number,
}
