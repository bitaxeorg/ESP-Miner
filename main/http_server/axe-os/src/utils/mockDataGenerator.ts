import { ChartDataPoint } from "../components/Chart/config";
import { Time } from "lightweight-charts";

export class MockDataGenerator {
  private currentValue: number;
  private currentTime: Date;
  private volatility: number;
  private trend: number;

  constructor(initialValue: number = 30, volatility: number = 0.1, trend: number = 0.001) {
    this.currentValue = initialValue;
    this.currentTime = new Date();
    this.volatility = volatility;
    this.trend = trend;
  }

  generateNextPoint(): ChartDataPoint {
    // Add some random volatility
    const randomChange = (Math.random() - 0.5) * this.volatility * this.currentValue;

    // Add slight upward trend
    const trendChange = this.trend * this.currentValue;

    // Update current value
    this.currentValue = Math.max(0.1, this.currentValue + randomChange + trendChange);

    // Increment time by 1 second
    this.currentTime = new Date(this.currentTime.getTime() + 1000);

    return {
      time: this.formatTime(this.currentTime),
      value: Number(this.currentValue.toFixed(2)),
    };
  }

  generateInitialData(points: number = 50): ChartDataPoint[] {
    const data: ChartDataPoint[] = [];

    // Start from 50 seconds ago
    this.currentTime = new Date(Date.now() - points * 1000);

    for (let i = 0; i < points; i++) {
      data.push(this.generateNextPoint());
    }

    return data;
  }

  private formatTime(date: Date): Time {
    // Return Unix timestamp in seconds as Time for real-time data
    return Math.floor(date.getTime() / 1000) as Time;
  }

  setVolatility(volatility: number): void {
    this.volatility = volatility;
  }

  setTrend(trend: number): void {
    this.trend = trend;
  }

  getCurrentValue(): number {
    return this.currentValue;
  }
}

export function createHashrateDataGenerator(): MockDataGenerator {
  return new MockDataGenerator(150, 0.05, 0.002); // Hashrate: starts at 150, low volatility, slight upward trend
}

export function createTemperatureDataGenerator(): MockDataGenerator {
  return new MockDataGenerator(65, 0.02, 0); // Temperature: starts at 65°C, very low volatility, no trend
}

export function createPowerDataGenerator(): MockDataGenerator {
  return new MockDataGenerator(1200, 0.03, 0.001); // Power: starts at 1200W, low volatility, slight upward trend
}
