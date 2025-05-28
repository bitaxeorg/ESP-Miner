import { SystemInfo } from "./types/systemInfo";

// Constants for firmware and webapp URLs
export const FIRMWARE_LATEST_URL =
  "https://acs-bitaxe-touch.s3.us-east-2.amazonaws.com/firmware/acs-esp-miner/latest/esp-miner.bin";
export const WEBAPP_LATEST_URL =
  "https://acs-bitaxe-touch.s3.us-east-2.amazonaws.com/web-app/acs-esp-miner/latest/www.bin";

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
    // First, download the firmware from the URL
    const firmwareResponse = await fetch(FIRMWARE_LATEST_URL);
    if (!firmwareResponse.ok) {
      throw new Error(`Failed to download firmware: ${firmwareResponse.status}`);
    }
    const firmwareBlob = await firmwareResponse.blob();

    console.log("Firmware blob:", firmwareBlob);

    // Then, send the firmware blob to the OTA endpoint
    const response = await fetch("/api/system/OTA", {
      method: "POST",
      headers: {
        "Content-Type": "application/octet-stream",
      },
      body: firmwareBlob,
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
    // Send the firmware blob directly to the OTA endpoint
    const response = await fetch("/api/system/OTA", {
      method: "POST",
      headers: {
        "Content-Type": "application/octet-stream",
      },
      body: file,
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
        window.location.hostname !== "localhost" ? window.location.hostname : "localhost:5173",
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

/**
 * Upload web app file
 * @param file - The web app file to upload
 * @returns A message indicating the web app update status
 */
export async function uploadWebApp(file: File): Promise<{ success: boolean; message: string }> {
  try {
    // Send the web app blob directly
    const response = await fetch("/api/system/upload-webapp", {
      method: "POST",
      headers: {
        "Content-Type": "application/octet-stream",
      },
      body: file,
    });

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const result = await response.json();
    console.log("Web app upload response:", result);
    return { success: true, message: result.message || "Web app uploaded successfully" };
  } catch (error) {
    console.error("Failed to upload web app:", error);
    return {
      success: false,
      message: error instanceof Error ? error.message : "Unknown error occurred",
    };
  }
}
