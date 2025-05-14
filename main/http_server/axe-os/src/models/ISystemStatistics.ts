import { eChartLabel } from './enum/eChartLabel';

export interface ISystemStatistics {
    currentTimestamp: number;
    chartY1Data: eChartLabel,
    chartY2Data: eChartLabel,
    statistics: number[][];
}
