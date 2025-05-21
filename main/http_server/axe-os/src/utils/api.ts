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
  stratumPassword?: string;
  fallbackStratumPassword?: string;
  stratumWorkerName?: string;
  fallbackStratumWorkerName?: string;
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

// Hook for toast will be imported in components that use these functions

// Constants for firmware and webapp URLs
export const FIRMWARE_LATEST_URL =
  "https://acs-bitaxe-touch.s3.us-east-2.amazonaws.com/firmware/acs-esp-miner/latest/esp-miner.bin";

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
 * @param stratumURL - The primary stratum URL
 * @param stratumPort - The primary stratum port
 * @param stratumPassword - Optional password for primary pool
 * @param stratumUser - Optional BTC address for primary pool
 * @param fallbackStratumURL - Optional fallback stratum URL
 * @param fallbackStratumPort - Optional fallback stratum port
 * @param fallbackStratumPassword - Optional password for fallback pool
 * @param fallbackStratumUser - Optional BTC address for fallback pool
 * @param stratumWorkerName - Optional worker name for primary pool
 * @param fallbackStratumWorkerName - Optional worker name for fallback pool
 * @returns The response from the API or a success message
 */
export async function updatePoolInfo(
  stratumURL: string,
  stratumPort: number,
  stratumPassword?: string,
  stratumUser?: string,
  fallbackStratumURL?: string,
  fallbackStratumPort?: number | null,
  fallbackStratumPassword?: string,
  fallbackStratumUser?: string,
  stratumWorkerName?: string,
  fallbackStratumWorkerName?: string
): Promise<any> {
  try {
    const payload: any = {
      stratumURL,
      stratumPort,
    };

    // Add optional fields if provided
    if (stratumPassword) {
      payload.stratumPassword = stratumPassword;
    }

    if (stratumUser) {
      payload.stratumUser = stratumUser;
    }

    if (fallbackStratumURL) {
      payload.fallbackStratumURL = fallbackStratumURL;
    }

    if (fallbackStratumPort) {
      payload.fallbackStratumPort = fallbackStratumPort;
    }

    if (fallbackStratumPassword) {
      payload.fallbackStratumPassword = fallbackStratumPassword;
    }

    if (fallbackStratumUser) {
      payload.fallbackStratumUser = fallbackStratumUser;
    }

    if (stratumWorkerName) {
      payload.stratumWorkerName = stratumWorkerName;
    }

    if (fallbackStratumWorkerName) {
      payload.fallbackStratumWorkerName = fallbackStratumWorkerName;
    }

    const response = await fetch("/api/system", {
      method: "PATCH",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
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
 * Update firmware with the latest version
 * @returns A message indicating the firmware update status
 */
export async function updateFirmware(): Promise<{ success: boolean; message: string }> {
  try {
    const response = await fetch("/api/system/update-firmware", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ firmwareUrl: FIRMWARE_LATEST_URL }),
    });

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const result = await response.json();
    console.log("Firmware update response:", result);
    return { success: true, message: result.message || "Firmware update initiated successfully" };
  } catch (error) {
    console.error("Failed to update firmware:", error);
    return {
      success: false,
      message: error instanceof Error ? error.message : "Unknown error occurred",
    };
  }
}

/**
 * Upload custom firmware file
 * @param file - The firmware file to upload
 * @returns A message indicating the firmware update status
 */
export async function uploadFirmware(file: File): Promise<{ success: boolean; message: string }> {
  try {
    const formData = new FormData();
    formData.append("firmware", file);

    const response = await fetch("/api/system/upload-firmware", {
      method: "POST",
      body: formData,
    });

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const result = await response.json();
    console.log("Firmware upload response:", result);
    return { success: true, message: result.message || "Firmware uploaded successfully" };
  } catch (error) {
    console.error("Failed to upload firmware:", error);
    return {
      success: false,
      message: error instanceof Error ? error.message : "Unknown error occurred",
    };
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
