// import { Miner } from "./types";

export interface SystemInfo {
  power: number;
  voltage: number;
  current: number;
  temp: number;
  vrTemp: number;
  hashRate: number;
  bestDiff: string;
  bestSessionDiff: string;
  stratumDiff: number;
  isUsingFallbackStratum: number;
  isPSRAMAvailable: number;
  freeHeap: number;
  coreVoltage: number;
  coreVoltageActual: number;
  frequency: number;
  ssid: string;
  macAddr: string;
  hostname: string;
  wifiStatus: string;
  apEnabled: number;
  sharesAccepted: number;
  sharesRejected: number;
  uptimeSeconds: number;
  asicCount: number;
  smallCoreCount: number;
  ASICModel: string;
  stratumURL: string;
  fallbackStratumURL: string;
  stratumPort: number;
  fallbackStratumPort: number;
  stratumUser: string;
  fallbackStratumUser: string;
  version: string;
  idfVersion: string;
  boardVersion: string;
  runningPartition: string;
  flipscreen: number;
  overheat_mode: number;
  invertscreen: number;
  invertfanpolarity: number;
  autofanspeed: number;
  fanspeed: number;
  fanrpm: number;
  status?: "online" | "offline" | "warning";
  ipAddress?: string;
}

export async function getSystemInfo(): Promise<SystemInfo> {
  try {
    const response = await fetch("/api/system/info");

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    return await response.json();
  } catch (error) {
    console.error("Failed to fetch system info:", error);
    throw error;
  }
}

/**
 * Fetch miners data
 * Uses the actual system info API to get real miner data
 */
export async function fetchMiners(): Promise<SystemInfo[]> {
  try {
    // Get the system info from the actual miner
    const minerInfo = await getSystemInfo();

    // Add IP address and status fields
    const enhancedMinerInfo: SystemInfo = {
      ...minerInfo,
      ipAddress:
        window.location.hostname !== "localhost" ? window.location.hostname : "192.168.4.1",
      status: "online", // Assume online since we're able to get data
    };

    // Return as an array with the single miner
    // In a multi-miner setup, this would be expanded to query multiple miners
    return [enhancedMinerInfo];
  } catch (error) {
    console.error("Failed to fetch miners:", error);
    // Return an empty array on error
    return [];
  }
}
