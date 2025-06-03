import { DeepPartial, ChartOptions, Time } from "lightweight-charts";

export const chartOption: DeepPartial<ChartOptions> = {
  layout: {
    textColor: "#333",
    background: {
      color: "#ffffff",
    },
  },
  grid: {
    vertLines: {
      color: "#f0f0f0",
    },
    horzLines: {
      color: "#f0f0f0",
    },
  },
  crosshair: {
    mode: 0,
  },
  rightPriceScale: {
    borderColor: "#cccccc",
  },
  timeScale: {
    borderColor: "#cccccc",
    timeVisible: true,
    secondsVisible: true,
  },
  handleScroll: {
    mouseWheel: true,
    pressedMouseMove: true,
    horzTouchDrag: true,
    vertTouchDrag: true,
  },
  handleScale: {
    axisPressedMouseMove: true,
    mouseWheel: true,
    pinch: true,
  },
  localization: {
    timeFormatter: (time: any) => {
      const date = new Date(time * 1000);
      return date.toLocaleTimeString();
    },
  },
};

export interface ChartDataPoint {
  time: Time;
  value: number;
}
