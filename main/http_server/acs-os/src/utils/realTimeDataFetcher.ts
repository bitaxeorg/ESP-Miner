import { ChartDataPoint } from "../components/Chart/config";
import { Time } from "lightweight-charts";
import { getSystemInfo } from "./api";
import { logger } from "./logger";
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
      logger.error("Failed to fetch system info:", error);
      return null;
    }
  }

  async generateInitialData(_points: number = 1): Promise<ChartDataPoint[]> {
    try {
      // Fetch current value only - no fake historical data
      const systemInfo = await getSystemInfo();
      const currentValue = this.extractValue(systemInfo, this.dataField);

      this.lastValue = currentValue;

      // Return single current data point
      return [{
        time: Math.floor(Date.now() / 1000) as Time,
        value: Number(currentValue.toFixed(2)),
      }];
    } catch (error) {
      logger.error("Failed to get initial data:", error);

      // If API fails, return empty array - chart will wait for real-time data
      return [];
    }
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
