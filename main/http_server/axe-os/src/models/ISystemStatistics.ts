interface IStatistic {
    hash: number;
    temp: number;
    power: number;
    timestamp: number;
}

export interface ISystemStatistics {
    currentTimestamp: number;
    statistics: IStatistic[];
}
