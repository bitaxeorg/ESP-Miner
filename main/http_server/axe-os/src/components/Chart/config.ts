import { DeepPartial, ChartOptions, Time } from "lightweight-charts";

export const chartOption: DeepPartial<ChartOptions> = {
  layout: {
    textColor: "#374151",
    background: {
      color: "#ffffff",
    },
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
    fontSize: 12,
  },
  grid: {
    vertLines: {
      color: "rgba(240, 240, 240, 0.6)",
      style: 0,
    },
    horzLines: {
      color: "rgba(240, 240, 240, 0.6)",
      style: 0,
    },
  },
  crosshair: {
    mode: 1,
    vertLine: {
      width: 1,
      color: 'rgba(156, 163, 175, 0.7)',
      style: 2,
    },
    horzLine: {
      width: 1,
      color: 'rgba(156, 163, 175, 0.7)',
      style: 2,
    },
  },
  rightPriceScale: {
    borderColor: "#e5e7eb",
    autoScale: true,
    scaleMargins: {
      top: 0.2,
      bottom: 0.2,
    },
    textColor: "#6b7280",
    alignLabels: true,
    borderVisible: true,
    entireTextOnly: false,
    visible: true,
  },
  timeScale: {
    borderColor: "#e5e7eb",
    timeVisible: true,
    secondsVisible: false,
    fixLeftEdge: false,
    fixRightEdge: true,
    lockVisibleTimeRangeOnResize: true,
    rightBarStaysOnScroll: true,
    tickMarkFormatter: (time: any, tickMarkType: any, locale: string) => {
      const date = new Date(time * 1000);

      switch (tickMarkType) {
        case 0:
          return date.getFullYear().toString();
        case 1:
          return date.toLocaleDateString('en-US', { month: 'short' });
        case 2:
          return date.getDate().toString();
        case 3:
          const minutes = date.getMinutes();
          const roundedMinutes = Math.floor(minutes / 5) * 5;
          const roundedDate = new Date(date);
          roundedDate.setMinutes(roundedMinutes, 0, 0);

          return roundedDate.toLocaleTimeString('en-US', {
            hour12: false,
            hour: '2-digit',
            minute: '2-digit'
          });
        default:
          return '';
      }
    },
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
  kineticScroll: {
    touch: true,
    mouse: true,
  },
  localization: {
    timeFormatter: (time: any) => {
      const date = new Date(time * 1000);

      return date.toLocaleTimeString('en-US', {
        hour12: false,
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
      });
    },
    priceFormatter: (price: number) => {
      if (price >= 1000) {
        return (price / 1000).toFixed(2) + 'K';
      }
      return price.toFixed(2);
    },
  },
};

export interface ChartDataPoint {
  time: Time;
  value: number;
}
