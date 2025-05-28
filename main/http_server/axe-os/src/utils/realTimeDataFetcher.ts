import { ChartDataPoint } from "../components/Chart/config";
import { Time } from "lightweight-charts";
import { getSystemInfo } from "./api";
import { SystemInfo } from "./types/systemInfo";

export type DataField =
  | "hashRate"
  | "temp"
  | "power"
  | "voltage"
  | "current"
  | "fanrpm"
  | "frequency";

export class RealTimeDataFetcher {
  private dataField: DataField;
  private currentTime: Date;
  private lastValue: number = 0;

  constructor(dataField: DataField) {
    this.dataField = dataField;
    this.currentTime = new Date();
  }

  async fetchNextPoint(): Promise<ChartDataPoint | null> {
    try {
      const systemInfo = await getSystemInfo();
      const value = this.extractValue(systemInfo, this.dataField);

      // Update time to current
      this.currentTime = new Date();
      this.lastValue = value;

      return {
        time: Math.floor(this.currentTime.getTime() / 1000) as Time,
        value: Number(value.toFixed(2)),
      };
    } catch (error) {
      console.error("Failed to fetch system info:", error);
      return null;
    }
  }

  async generateInitialData(points: number = 50): Promise<ChartDataPoint[]> {
    const data: ChartDataPoint[] = [];

    try {
      // For initial data, we'll fetch current value and create a history
      // In a real scenario, you might want to fetch historical data from a database
      const systemInfo = await getSystemInfo();
      const currentValue = this.extractValue(systemInfo, this.dataField);

      // Generate historical points with slight variations around current value
      const startTime = Date.now() - points * 2000; // 2 seconds per point

      for (let i = 0; i < points; i++) {
        // Add small random variations to simulate historical data
        const variation = (Math.random() - 0.5) * 0.1 * currentValue;
        const value = Math.max(0.1, currentValue + variation);

        data.push({
          time: Math.floor((startTime + i * 2000) / 1000) as Time,
          value: Number(value.toFixed(2)),
        });
      }

      this.lastValue = currentValue;
    } catch (error) {
      console.error("Failed to generate initial data:", error);
    }

    return data;
  }

  private extractValue(systemInfo: SystemInfo, field: DataField): number {
    switch (field) {
      case "hashRate":
        return systemInfo.hashRate;
      case "temp":
        return systemInfo.temp;
      case "power":
        return systemInfo.power;
      case "voltage":
        return systemInfo.voltage;
      case "current":
        return systemInfo.current;
      case "fanrpm":
        return systemInfo.fanrpm;
      case "frequency":
        return systemInfo.frequency;
      default:
        return 0;
    }
  }

  getLastValue(): number {
    return this.lastValue;
  }

  getDataField(): DataField {
    return this.dataField;
  }
}

// Factory functions for different data types
export function createHashrateDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("hashRate");
}

export function createTemperatureDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("temp");
}

export function createPowerDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("power");
}

export function createVoltageDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("voltage");
}

export function createCurrentDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("current");
}

export function createFanRpmDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("fanrpm");
}

export function createFrequencyDataFetcher(): RealTimeDataFetcher {
  return new RealTimeDataFetcher("frequency");
}
