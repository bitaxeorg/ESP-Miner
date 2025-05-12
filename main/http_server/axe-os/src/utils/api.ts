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
 * Update pool information
 * @param stratumURL - The stratum URL to set
 * @param stratumPort - The stratum port to set
 * @returns The response from the API or a success message
 */
export async function updatePoolInfo(stratumURL: string, stratumPort: number): Promise<any> {
  try {
    const response = await fetch("/api/system", {
      method: "PATCH",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        stratumURL,
        stratumPort,
      }),
    });

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const text = await response.text();

    // If the response has content, try to parse it as JSON
    if (text.trim()) {
      try {
        const result = JSON.parse(text);
        console.log("Pool info update response:", result);
        return result;
      } catch (parseError) {
        console.log("Response is not JSON:", text);
        return { success: true, message: "Pool information updated successfully" };
      }
    }

    // For empty responses with 200 status
    console.log("Pool info update successful (empty response)");
    return { success: true, message: "Pool information updated successfully" };
  } catch (error) {
    console.error("Failed to update pool info:", error);
    throw error;
  }
}

/**
 * Restart the system
 * @returns A message indicating the system will restart
 */
export async function restartSystem(): Promise<string> {
  try {
    const response = await fetch("/api/system/restart", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({}),
    });

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const result = await response.text();
    console.log("System restart response:", result);
    return result;
  } catch (error) {
    console.error("Failed to restart system:", error);
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
