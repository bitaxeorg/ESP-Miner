import { SystemInfo } from "./types/systemInfo";

// Re-export SystemInfo type for convenience
export type { SystemInfo };

// Global reference for overheat warning system
let globalOverheatWarningCheck: ((systemInfo: SystemInfo) => void) | null = null;

export function setOverheatWarningCheck(checkFn: (systemInfo: SystemInfo) => void) {
  globalOverheatWarningCheck = checkFn;
}

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

    const systemInfo = await response.json();

    // Trigger overheat warning check if available
    if (globalOverheatWarningCheck) {
      globalOverheatWarningCheck(systemInfo);
    }

    return systemInfo;
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
        window.location.hostname !== "localhost" ? window.location.hostname : "localhost:5173",
      status: "online", // Assume online since we're able to get data
    };

    // Trigger overheat warning check if available
    if (globalOverheatWarningCheck) {
      globalOverheatWarningCheck(enhancedMinerInfo);
    }

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

export async function setSSID(
  ssid: string,
  wifiPass: string
): Promise<{ success: boolean; message: string }> {
  try {
    const response = await fetch("/api/system", {
      method: "PATCH",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ ssid, wifiPass }),
    });

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const result = await response.json();
    console.log("SSID set response:", result);
    return { success: true, message: result.message || "SSID set successfully" };
  } catch (error) {
    console.error("Failed to set SSID:", error);
    return {
      success: false,
      message: error instanceof Error ? error.message : "Unknown error occurred",
    };
  }
}

/**
 * Update fan settings
 * @param autofanspeed - Whether automatic fan control is enabled (1) or disabled (0)
 * @param fanspeed - Fan speed percentage (only used when autofanspeed is 0)
 * @returns A message indicating the fan settings update status
 */
export async function updateFanSettings(
  autofanspeed: number,
  fanspeed?: number
): Promise<{ success: boolean; message: string }> {
  try {
    const payload: any = {
      autofanspeed,
    };

    // Only include fanspeed if autofanspeed is disabled (0)
    if (autofanspeed === 0 && fanspeed !== undefined) {
      payload.fanspeed = fanspeed;
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
        console.log("Fan settings update response:", result);
        return result;
      } catch (parseError) {
        console.log("Response is not JSON:", text);
        return { success: true, message: "Fan settings updated successfully" };
      }
    }

    // For empty responses with 200 status
    console.log("Fan settings update successful (empty response)");
    return { success: true, message: "Fan settings updated successfully" };
  } catch (error) {
    console.error("Failed to update fan settings:", error);
    return {
      success: false,
      message: error instanceof Error ? error.message : "Unknown error occurred",
    };
  }
}

/**
 * Update ASIC settings
 * @param coreVoltage - Core voltage in mV
 * @param frequency - Frequency in MHz
 * @returns A message indicating the ASIC settings update status
 */
export async function updateASICSettings(
  coreVoltage: number,
  frequency: number
): Promise<{ success: boolean; message: string }> {
  try {
    const payload = {
      coreVoltage,
      frequency,
    };

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
        console.log("ASIC settings update response:", result);
        return result;
      } catch (parseError) {
        console.log("Response is not JSON:", text);
        return { success: true, message: "ASIC settings updated successfully" };
      }
    }

    // For empty responses with 200 status
    console.log("ASIC settings update successful (empty response)");
    return { success: true, message: "ASIC settings updated successfully" };
  } catch (error) {
    console.error("Failed to update ASIC settings:", error);
    return {
      success: false,
      message: error instanceof Error ? error.message : "Unknown error occurred",
    };
  }
}

/**
 * Update preset settings
 * @param presetName - The preset name: "quiet", "balanced", or "turbo"
 * @returns A message indicating the preset update status
 */
export async function updatePresetSettings(
  presetName: string
): Promise<{ status: string; message: string; updatedSettings: []; timestamps: any }> {
  try {
    const payload = {
      presetName,
    };

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
        console.log("Preset settings update response:", result);
        return result;
      } catch (parseError) {
        console.log("Response is not JSON:", text);
        return {
          status: "success",
          message: `${presetName} mode applied successfully`,
          updatedSettings: [],
          timestamps: [],
        };
      }
    }

    // For empty responses with 200 status
    console.log("Preset settings update successful (empty response)");
    return {
      status: "success",
      message: `${presetName} mode applied successfully`,
      updatedSettings: [],
      timestamps: [],
    };
  } catch (error) {
    console.error("Failed to update preset settings:", error);
    return {
      status: "error",
      message: error instanceof Error ? error.message : "Unknown error occurred",
      updatedSettings: [],
      timestamps: [],
    };
  }
}

// GitHub Release Types
export interface GitHubRelease {
  tag_name: string;
  name: string;
  body: string;
  published_at: string;
  prerelease: boolean;
  draft: boolean;
  html_url: string;
}

export interface VersionInfo {
  current: string | null;
  latest: string | null;
  hasUpdate: boolean;
  isLoading: boolean;
  error: string | null;
  lastChecked: Date | null;
}

/**
 * Fetch the latest release from GitHub
 * @returns The latest release information
 */
export async function fetchLatestRelease(): Promise<GitHubRelease> {
  try {
    const response = await fetch(
      "https://api.github.com/repos/Advanced-Crypto-Services/acs-esp-miner/releases/latest"
    );

    if (!response.ok) {
      throw new Error(`GitHub API error: ${response.status}`);
    }

    const release = await response.json();
    console.log("Latest release:", release.tag_name);
    return release;
  } catch (error) {
    console.error("Failed to fetch latest release:", error);
    throw error;
  }
}

/**
 * Compare two version strings
 * Handles various version formats like "v1.2.3", "ACSOSv0.1-U2", etc.
 * @param current - Current version string
 * @param latest - Latest version string
 * @returns true if latest is newer than current
 */
export function isNewerVersion(current: string, latest: string): boolean {
  if (!current || !latest) return false;

  // Clean up version strings (remove 'v' prefix, handle special formats)
  const cleanCurrent = cleanVersionString(current);
  const cleanLatest = cleanVersionString(latest);

  console.log("Version comparison:", {
    original: { current, latest },
    cleaned: { current: cleanCurrent, latest: cleanLatest },
    areEqual: cleanCurrent === cleanLatest,
    stringComparison: cleanLatest > cleanCurrent,
  });

  // If versions are exactly the same, no update needed
  if (cleanCurrent === cleanLatest) {
    return false;
  }

  // For ACSOS versions, handle the specific format
  if (cleanCurrent.includes("-u") && cleanLatest.includes("-u")) {
    return compareAcsosVersions(cleanCurrent, cleanLatest);
  }

  // For other versions, try semver-style comparison
  return compareSemverVersions(cleanCurrent, cleanLatest);
}

/**
 * Clean and normalize version strings for comparison
 * @param version - Raw version string
 * @returns Cleaned version string
 */
function cleanVersionString(version: string): string {
  // Remove 'v' prefix if present
  let cleaned = version.replace(/^v/i, "");

  // Handle ACSOS format like "ACSOSv0.1-U2"
  if (cleaned.startsWith("ACSOS")) {
    cleaned = cleaned.replace(/^ACSOS/i, "");
    if (cleaned.startsWith("v")) {
      cleaned = cleaned.replace(/^v/i, "");
    }
  }

  return cleaned.toLowerCase();
}

/**
 * Compare ACSOS-style versions like "0.1-u1" vs "0.1-u2"
 * @param current - Current version string (cleaned)
 * @param latest - Latest version string (cleaned)
 * @returns true if latest is newer than current
 */
function compareAcsosVersions(current: string, latest: string): boolean {
  // Parse format like "0.1-u2"
  const parseAcsosVersion = (version: string) => {
    const match = version.match(/^(\d+)\.(\d+)-u(\d+)$/);
    if (!match) return null;
    return {
      major: parseInt(match[1], 10),
      minor: parseInt(match[2], 10),
      update: parseInt(match[3], 10),
    };
  };

  const currentParts = parseAcsosVersion(current);
  const latestParts = parseAcsosVersion(latest);

  if (!currentParts || !latestParts) {
    // Fallback to string comparison if parsing fails
    return latest > current;
  }

  // Compare major.minor.update
  if (latestParts.major !== currentParts.major) {
    return latestParts.major > currentParts.major;
  }
  if (latestParts.minor !== currentParts.minor) {
    return latestParts.minor > currentParts.minor;
  }
  return latestParts.update > currentParts.update;
}

/**
 * Compare semver-style versions like "1.2.3" vs "1.2.4"
 * @param current - Current version string (cleaned)
 * @param latest - Latest version string (cleaned)
 * @returns true if latest is newer than current
 */
function compareSemverVersions(current: string, latest: string): boolean {
  // Parse semver format
  const parseSemver = (version: string) => {
    const parts = version.split(/[.-]/).map((part) => {
      const num = parseInt(part, 10);
      return isNaN(num) ? 0 : num;
    });
    return {
      major: parts[0] || 0,
      minor: parts[1] || 0,
      patch: parts[2] || 0,
    };
  };

  const currentParts = parseSemver(current);
  const latestParts = parseSemver(latest);

  // Compare major.minor.patch
  if (latestParts.major !== currentParts.major) {
    return latestParts.major > currentParts.major;
  }
  if (latestParts.minor !== currentParts.minor) {
    return latestParts.minor > currentParts.minor;
  }
  return latestParts.patch > currentParts.patch;
}

/**
 * Get comprehensive version information including update availability
 * @param currentVersion - Current firmware version
 * @returns Version information object
 */
export async function getVersionInfo(currentVersion: string | null): Promise<VersionInfo> {
  try {
    const release = await fetchLatestRelease();
    const latestVersion = release.tag_name;

    return {
      current: currentVersion,
      latest: latestVersion,
      hasUpdate: currentVersion ? isNewerVersion(currentVersion, latestVersion) : true,
      isLoading: false,
      error: null,
      lastChecked: new Date(),
    };
  } catch (error) {
    return {
      current: currentVersion,
      latest: null,
      hasUpdate: false,
      isLoading: false,
      error: error instanceof Error ? error.message : "Failed to check for updates",
      lastChecked: new Date(),
    };
  }
}

/**
 * Log event interface
 */
export interface LogEvent {
  timestamp: number;
  type: string;
  level: "info" | "warning" | "error" | "critical";
  message: string;
  data?: Record<string, any>;
}

/**
 * Fetch recent system event logs
 * @param limit - Maximum number of events to return (default: 50, max: 100)
 * @returns Array of recent log events
 */
export async function fetchRecentLogs(limit?: number): Promise<{
  events: LogEvent[];
  count: number;
}> {
  try {
    const params = new URLSearchParams();
    if (limit !== undefined) {
      params.append("limit", limit.toString());
    }

    const url = `/api/logs/recent${params.toString() ? `?${params.toString()}` : ""}`;
    const response = await fetch(url);

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const data = await response.json();
    return {
      events: data.events || [],
      count: data.count || 0,
    };
  } catch (error) {
    console.error("Failed to fetch recent logs:", error);
    return {
      events: [],
      count: 0,
    };
  }
}

/**
 * Fetch error logs (errors and critical events only)
 * @param limit - Maximum number of errors to return (default: all errors)
 * @returns Array of error log events with additional metadata
 */
export async function fetchErrorLogs(limit?: number): Promise<{
  errors: LogEvent[];
  count: number;
  totalErrors: number;
  lastError?: number;
}> {
  try {
    const params = new URLSearchParams();
    if (limit !== undefined) {
      params.append("limit", limit.toString());
    }

    const url = `/api/logs/errors${params.toString() ? `?${params.toString()}` : ""}`;
    const response = await fetch(url);

    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }

    const data = await response.json();
    return {
      errors: data.errors || [],
      count: data.count || 0,
      totalErrors: data.totalErrors || 0,
      lastError: data.lastError,
    };
  } catch (error) {
    console.error("Failed to fetch error logs:", error);
    return {
      errors: [],
      count: 0,
      totalErrors: 0,
    };
  }
}
