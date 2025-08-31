import { eChartLabel } from './enum/eChartLabel';

export interface ISystemStatistics {
    currentTimestamp: number;
    chartY1Data: string;
    chartY2Data: string;
    statistics: number[][];
}
