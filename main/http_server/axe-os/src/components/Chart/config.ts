import { DeepPartial, ChartOptions, AreaSeriesOptions, Time } from "lightweight-charts";

export const chartOption: DeepPartial<ChartOptions> = {
  crosshair: {
    vertLine: {
      color: "rgba(100, 100, 100, 0.4)",
    },
    horzLine: {
      color: "rgba(100, 100, 100, 0.4)",
      labelBackgroundColor: "#f5f5fe",
    },
  },
  layout: {
    background: { color: "transparent" },
    textColor: "#787F83",
    fontFamily: "Danila",
    fontSize: 14,
  },
  grid: {
    vertLines: {
      visible: false,
    },
    horzLines: {
      visible: false,
    },
  },
  leftPriceScale: {
    visible: true,
    borderVisible: true,
  },
  rightPriceScale: {
    borderVisible: true,
  },
  timeScale: {
    fixLeftEdge: true,
    fixRightEdge: true,
    lockVisibleTimeRangeOnResize: true,
    rightBarStaysOnScroll: true,
    visible: true,
  },
  handleScroll: false,
  handleScale: false,
};

export const areaOption: DeepPartial<AreaSeriesOptions> = {
  lineColor: "rgb(100, 100, 100)",
  lineWidth: 1,
  topColor: "rgba(100, 100, 100, 0.4)",
  bottomColor: "rgba(0, 0, 0, 0)",
  crosshairMarkerVisible: true,
  crosshairMarkerRadius: 8,
  crosshairMarkerBorderColor: "rgba(100, 100, 100, 0.4)",
  crosshairMarkerBackgroundColor: "#FFF",
};

export interface ChartDataPoint {
  time: Time;
  value: number;
}

export const INITIAL_DATA: ChartDataPoint[] = [
  { time: Math.floor(new Date("2022-10-01").getTime() / 1000) as Time, value: 21.93 },
  { time: Math.floor(new Date("2022-10-02").getTime() / 1000) as Time, value: 38.43 },
  { time: Math.floor(new Date("2022-10-03").getTime() / 1000) as Time, value: 15.31 },
  { time: Math.floor(new Date("2022-10-04").getTime() / 1000) as Time, value: 29.02 },
  { time: Math.floor(new Date("2022-10-05").getTime() / 1000) as Time, value: 42.01 },
  { time: Math.floor(new Date("2022-10-06").getTime() / 1000) as Time, value: 22.31 },
  { time: Math.floor(new Date("2022-10-07").getTime() / 1000) as Time, value: 35.24 },
  { time: Math.floor(new Date("2022-10-08").getTime() / 1000) as Time, value: 15.38 },
  { time: Math.floor(new Date("2022-10-09").getTime() / 1000) as Time, value: 31.29 },
  { time: Math.floor(new Date("2022-10-10").getTime() / 1000) as Time, value: 45.3 },
  { time: Math.floor(new Date("2022-10-11").getTime() / 1000) as Time, value: 18.22 },
  { time: Math.floor(new Date("2022-10-12").getTime() / 1000) as Time, value: 27.89 },
  { time: Math.floor(new Date("2022-10-13").getTime() / 1000) as Time, value: 32.76 },
  { time: Math.floor(new Date("2022-10-14").getTime() / 1000) as Time, value: 12.45 },
  { time: Math.floor(new Date("2022-10-15").getTime() / 1000) as Time, value: 40.1 },
];
